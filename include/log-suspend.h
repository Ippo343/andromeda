#pragma once

#include <atomic>
#include <cstdint>

// Coordination between SimpleFileLog (loggers.h) and the OTA filesystem write
// (#63). No Arduino/FreeRTOS deps so the native suite exercises the state
// machine directly.
//
// The problem: OtaUpdater sets a "logging suspended" flag and then calls
// LittleFS.end() microseconds later. SimpleFileLog::write() checks that flag
// once per byte and then goes on to _logFile.write() / flush() /
// checkRotation() (which itself does LittleFS.exists/remove/rename/open). Any
// other task - the render task logs "Picked new effect" on every transition -
// can be part-way through that body when LittleFS.end() unmounts the
// filesystem out from under it: a deref of freed lfs structures, or a partial
// block write onto the flash region the raw OTA write is about to claim. This
// is the "Corrupted dir pair" corruption the flag was meant to prevent; the
// flag closed the common case but not the interleaved one.
//
// The fix is a real handoff: a writer holds a slot for the whole duration of
// its write body, and the suspend path waits for every slot to drain before
// it returns (and thus before LittleFS.end() runs).

namespace LogSuspend
{

inline std::atomic<bool> g_suspended{false};
inline std::atomic<int> g_writersInFlight{0};

// Called at the top of SimpleFileLog::write(). Returns false (write nothing)
// once a suspend is in progress. On true, the caller MUST pair it with
// endWrite(). The re-check after the increment closes the window where
// beginSuspend() runs between the load and the fetch_add.
inline bool beginWrite()
{
    if (g_suspended.load(std::memory_order_acquire)) return false;
    g_writersInFlight.fetch_add(1, std::memory_order_acq_rel);
    if (g_suspended.load(std::memory_order_acquire))
    {
        g_writersInFlight.fetch_sub(1, std::memory_order_acq_rel);
        return false;
    }
    return true;
}

inline void endWrite() { g_writersInFlight.fetch_sub(1, std::memory_order_acq_rel); }

// Number of writers currently inside a begin/endWrite() pair.
inline int writersInFlight() { return g_writersInFlight.load(std::memory_order_acquire); }

// Flip the flag so no new writer can enter. Does NOT wait - see drain().
inline void beginSuspend() { g_suspended.store(true, std::memory_order_release); }

// True once the flag is set and no writer is still inside a write body - i.e.
// it is now safe to LittleFS.end().
inline bool drained()
{
    return g_suspended.load(std::memory_order_acquire) && writersInFlight() == 0;
}

// Set the flag and spin (via the caller's yield - vTaskDelay on the target, a
// no-op / writer-releasing stub in tests) until every in-flight writer has
// left. `maxSpins` bounds the wait so a stuck writer can't hang the OTA task
// forever; returns true if it drained, false if it bailed on the bound.
template <typename YieldFn>
inline bool suspendAndDrain(YieldFn yield, int maxSpins = 1000)
{
    beginSuspend();
    for (int i = 0; i < maxSpins; i++)
    {
        if (writersInFlight() == 0) return true;
        yield();
    }
    return writersInFlight() == 0;
}

// Test-only: back to the initial state.
inline void resetForTest()
{
    g_suspended.store(false, std::memory_order_release);
    g_writersInFlight.store(0, std::memory_order_release);
}

}  // namespace LogSuspend
