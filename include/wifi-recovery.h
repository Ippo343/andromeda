#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>

#include "utils.h"

// Pure, dependency-free (no WiFi/FreeRTOS includes) backoff + AP-fallback state machine for a
// mid-run WiFi disconnect. Deliberately free of Arduino/WiFi includes so it's natively
// unit-testable, mirroring ws-command-parser.h's extraction of comms.cpp logic.
//
// Before this existed, a disconnect after boot (router power-cycled, renamed, or its password
// changed) drove an unbounded WiFi.reconnect() loop with no backoff and no escape hatch - the
// only recovery was a manual reflash/NVS erase. This gives the caller a bounded reconnect
// schedule and, if the link never comes back, a one-time signal to fall back into the setup
// AP so the device stays reachable by someone standing next to it.
class WifiRecovery
{
   public:
    static constexpr milliseconds_t INITIAL_BACKOFF_MS = 1000;
    // Capped well below AP_FALLBACK_DEAD_TIME_MS so several reconnect attempts still
    // land inside the dead-time window (1s,2s,4s,8s,15s,15s,... -> ~7 tries in a minute)
    // rather than one long wait eating the whole window.
    static constexpr milliseconds_t MAX_BACKOFF_MS = 15000;
    // How long a mid-run outage may persist before we give up on the saved network and
    // fall back to the setup AP so someone standing next to the device can still reach it.
    static constexpr milliseconds_t AP_FALLBACK_DEAD_TIME_MS = 1UL * 60 * 1000;  // 1 minute

    enum class Action
    {
        None,
        Reconnect,
        EnterAPMode
    };

    enum class Event : uint8_t
    {
        Disconnected,
        Connected
    };

    // The link-state transitions the WiFi event task posts here (from the
    // ARDUINO_EVENT_WIFI_STA_* handler), drained by tick() on the monitor
    // task. This crossing used to be direct calls to onDisconnected()/
    // onConnected() from the event task while tick() ran on another - a
    // non-atomic read-modify-write over five fields on two tasks that can be
    // on different cores. Concrete break: tick() reads disconnected_ (still
    // true), the event task lands onConnected() (clears it, resets backoff),
    // tick() resumes and returns Reconnect on a link that just came up. The
    // queue makes tick() the only writer of the state machine.
    //
    // Lock-free single-producer / single-consumer: postEvent() is only ever
    // called from the event task, drainEvents() only from tick().
    void postEvent(Event e, milliseconds_t now)
    {
        const uint32_t w = writeIdx_.load(std::memory_order_relaxed);
        if (w - readIdx_.load(std::memory_order_acquire) >= QUEUE_CAP) return;  // full: drop
        events_[w % QUEUE_CAP] = {e, now};
        writeIdx_.store(w + 1, std::memory_order_release);
    }

    // Kept public: the native tests drive the state machine directly through
    // these, and they double as the drain appliers. onDisconnected() is safe
    // to call repeatedly mid-outage - only the first call in an outage starts
    // the clock and backoff schedule.
    void onDisconnected(milliseconds_t now)
    {
        if (disconnected_) return;
        disconnected_ = true;
        apFallbackSignaled_ = false;
        disconnectedAt_ = now;
        nextAttemptAt_ = now;  // try immediately on the first tick
        backoffMs_ = INITIAL_BACKOFF_MS;
    }

    void onConnected()
    {
        disconnected_ = false;
        apFallbackSignaled_ = false;
        backoffMs_ = INITIAL_BACKOFF_MS;
    }

    // Call periodically (roughly once a second is plenty - the backoff granularity is
    // seconds) while possibly disconnected. jitterMs is added to the computed backoff before
    // scheduling the next attempt - pass e.g. a small random value so multiple devices on the
    // same network don't retry in lockstep; pass 0 for deterministic tests.
    //
    // Returns Reconnect at most once per backoff window (the caller should call
    // WiFi.reconnect() on it), then EnterAPMode exactly once after AP_FALLBACK_DEAD_TIME_MS
    // has elapsed since the disconnect with no onConnected() call, then None forever after
    // that for this outage - the caller is expected to actually switch to AP mode and either
    // call onConnected() or start a fresh outage via onDisconnected() from there.
    Action tick(milliseconds_t now, milliseconds_t jitterMs = 0)
    {
        drainEvents();

        if (!disconnected_ || apFallbackSignaled_) return Action::None;

        // Unsigned subtraction wraps correctly across a millis() rollover, same idiom used
        // throughout mission-control.cpp (e.g. calcBrightness()/updateTransition()).
        if (now - disconnectedAt_ >= AP_FALLBACK_DEAD_TIME_MS)
        {
            apFallbackSignaled_ = true;
            return Action::EnterAPMode;
        }

        if (now - nextAttemptAt_ < HALF_RANGE)
        {
            nextAttemptAt_ = now + backoffMs_ + jitterMs;
            backoffMs_ = std::min<milliseconds_t>(backoffMs_ * 2, MAX_BACKOFF_MS);
            return Action::Reconnect;
        }

        return Action::None;
    }

   private:
    // Half the wraparound range: `now - nextAttemptAt_` (unsigned) is small and non-negative
    // once now has reached nextAttemptAt_, and close to the full range while nextAttemptAt_ is
    // still in the future - this is the same "is the deadline due yet" comparison used
    // elsewhere (e.g. MissionControl's `t >= nextTransition`), spelled to tolerate wraparound.
    static constexpr milliseconds_t HALF_RANGE = ~0UL / 2;

    // Apply every event the WiFi task has posted since the last tick, in
    // order. tick() is the sole consumer, so the state machine only ever
    // mutates from this one task.
    void drainEvents()
    {
        uint32_t r = readIdx_.load(std::memory_order_relaxed);
        const uint32_t w = writeIdx_.load(std::memory_order_acquire);
        for (; r != w; ++r)
        {
            const QueuedEvent& qe = events_[r % QUEUE_CAP];
            if (qe.event == Event::Disconnected)
                onDisconnected(qe.at);
            else
                onConnected();
        }
        readIdx_.store(r, std::memory_order_release);
    }

    struct QueuedEvent
    {
        Event event = Event::Connected;
        milliseconds_t at = 0;
    };
    // 8 slots is comfortably more than the handful of transitions a real
    // outage produces between two 1 Hz ticks; a full queue drops the newest
    // (tick() re-derives state from disconnected_ anyway).
    static constexpr uint32_t QUEUE_CAP = 8;
    QueuedEvent events_[QUEUE_CAP];
    std::atomic<uint32_t> writeIdx_{0};
    std::atomic<uint32_t> readIdx_{0};

    bool disconnected_ = false;
    bool apFallbackSignaled_ = false;
    milliseconds_t disconnectedAt_ = 0;
    milliseconds_t nextAttemptAt_ = 0;
    milliseconds_t backoffMs_ = INITIAL_BACKOFF_MS;
};
