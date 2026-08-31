#pragma once

#include <algorithm>

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
    static constexpr milliseconds_t MAX_BACKOFF_MS = 60000;
    static constexpr milliseconds_t AP_FALLBACK_DEAD_TIME_MS = 5UL * 60 * 1000;  // 5 minutes

    enum class Action
    {
        None,
        Reconnect,
        EnterAPMode
    };

    // Call once when the link drops (e.g. ARDUINO_EVENT_WIFI_STA_DISCONNECTED). Safe to call
    // repeatedly while already disconnected - only the first call in an outage starts the
    // clock and backoff schedule.
    void onDisconnected(milliseconds_t now)
    {
        if (disconnected_) return;
        disconnected_ = true;
        apFallbackSignaled_ = false;
        disconnectedAt_ = now;
        nextAttemptAt_ = now;  // try immediately on the first tick
        backoffMs_ = INITIAL_BACKOFF_MS;
    }

    // Call once when the link comes back (e.g. ARDUINO_EVENT_WIFI_STA_GOT_IP).
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

    bool disconnected_ = false;
    bool apFallbackSignaled_ = false;
    milliseconds_t disconnectedAt_ = 0;
    milliseconds_t nextAttemptAt_ = 0;
    milliseconds_t backoffMs_ = INITIAL_BACKOFF_MS;
};
