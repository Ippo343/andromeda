#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// In-RAM circular buffer holding the most recent log output, so the web UI can
// pull a log tail over HTTP without a serial cable and without reading the
// LittleFS log file the file logger is concurrently writing to.
//
// Pure and Arduino-free by design - mirrors ws-command-parser.h /
// ws-state-builder.h - so it compiles into the native unit-test build. The only
// platform touch is a per-call critical section: Log.* is called from several
// FreeRTOS tasks (main loop, comms task, WiFi), so writes and the snapshot read
// are serialised with a portMUX spinlock on-device and a no-op on the host.
#if defined(NATIVE_BUILD)
#define LOG_RING_ENTER_CRITICAL() ((void)0)
#define LOG_RING_EXIT_CRITICAL() ((void)0)
#else
#include <freertos/FreeRTOS.h>
#define LOG_RING_ENTER_CRITICAL() portENTER_CRITICAL(&_mux)
#define LOG_RING_EXIT_CRITICAL() portEXIT_CRITICAL(&_mux)
#endif

class LogRing
{
   public:
    static constexpr size_t CAP = 8192;  // ~80-100 formatted log lines

    static LogRing& Instance()
    {
        static LogRing instance;
        return instance;
    }

    LogRing(const LogRing&) = delete;
    LogRing& operator=(const LogRing&) = delete;

    // Append up to `n` bytes, wrapping. If a single call carries more than CAP
    // bytes only the trailing CAP are kept (the rest could never survive).
    void write(const uint8_t* data, size_t n)
    {
        if (!data || n == 0) return;
        if (n > CAP)
        {
            data += (n - CAP);
            n = CAP;
        }

        LOG_RING_ENTER_CRITICAL();
        for (size_t i = 0; i < n; ++i)
        {
            _buf[_head++] = static_cast<char>(data[i]);
            if (_head == CAP)
            {
                _head = 0;
                _full = true;
            }
        }
        LOG_RING_EXIT_CRITICAL();
    }

    // Copy the buffered bytes oldest-to-newest into `dst`. When `cap` is smaller
    // than the amount buffered, copies the newest `cap` bytes (it is a tail).
    // Returns bytes written; `dst` is not NUL-terminated.
    size_t snapshot(char* dst, size_t cap) const
    {
        if (!dst || cap == 0) return 0;

        LOG_RING_ENTER_CRITICAL();
        const size_t avail = _full ? CAP : _head;
        const size_t oldest = _full ? _head : 0;
        const size_t skip = (avail > cap) ? (avail - cap) : 0;
        const size_t count = avail - skip;
        const size_t start = (oldest + skip) % CAP;

        size_t first = count;
        if (start + first > CAP) first = CAP - start;
        std::memcpy(dst, _buf + start, first);
        if (first < count) std::memcpy(dst + first, _buf, count - first);
        LOG_RING_EXIT_CRITICAL();

        return count;
    }

   private:
    LogRing() = default;

#if !defined(NATIVE_BUILD)
    mutable portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
#endif

    char _buf[CAP] = {};
    size_t _head = 0;
    bool _full = false;

#ifdef UNIT_TEST
    // Test-only access so native unit tests can return the shared singleton to
    // a known-empty state between cases (the ctor is private on purpose).
    friend class LogRingTestAccess;
#endif
};
