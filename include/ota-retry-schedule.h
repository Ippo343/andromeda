#pragma once

#include <cstdint>

#include "ota-start-gate.h"

// Pure retry-cadence logic for main.cpp's OtaAutoCheck task (#63). Split out
// the same way OtaStartGate/OtaEligibility are, so the native suite
// (test/test_ota_retry_schedule) covers the arithmetic that decides how long
// the task sleeps before its next attempt.
//
// Two bugs lived in the inline version of this logic:
//   - the filesystem self-repair loop cleared its "still damaged" flag the
//     moment a worker was *spawned*, not once it actually succeeded - a
//     transient failure (a network hiccup, a bad manifest) left the device
//     falling back to the once-a-day check forever, with no web UI and no
//     in-band way back.
//   - a boot where startCheck() couldn't run yet (most commonly: WiFi isn't
//     connected 10s after boot, e.g. still in AP setup mode) discarded that
//     outcome and slept a full day regardless, so a device power-cycled daily
//     could go a very long time without ever actually checking for updates.
//
// The fix in both cases is the same shape: only the daily cadence is "we
// actually ran"; anything else backs off on a short, capped schedule instead.
namespace OtaRetrySchedule
{

// A worker that was actually spawned means either it succeeds (the update
// path reboots the device, so this schedule stops mattering) or it fails and
// gets retried on the short schedule below via the *next* outcome - so
// clearing "still needs attention" here would repeat the original bug.
constexpr uint32_t MIN_RETRY_DELAY_MS = 60ULL * 1000;               // 1 min
constexpr uint32_t MAX_RETRY_DELAY_MS = 30ULL * 60 * 1000;          // 30 min cap
constexpr uint32_t STEADY_STATE_DELAY_MS = 24ULL * 60 * 60 * 1000;  // 24 h

// How long to sleep before the next attempt.
//   fsDamaged   - true for the life of the boot once setup() found LittleFS
//                 unmountable (see fs-health.h); the device has no web UI
//                 until a repair succeeds and reboots it.
//   last        - the OtaStartGate::Outcome of the attempt just made
//                 (startUpdate() while repairing, startCheck() otherwise).
//   prevDelayMs - the delay this function returned last time (0 on the first
//                 call), so a run of unsuccessful attempts backs off instead
//                 of hammering GitHub every minute.
inline uint32_t nextDelayMs(bool fsDamaged, OtaStartGate::Outcome last, uint32_t prevDelayMs)
{
    // Still damaged: never settle into the daily cadence, whether or not a
    // worker was spawned - success reboots the device (this code stops
    // running), so reaching here again only ever means the repair hasn't
    // taken yet and must keep being retried.
    if (fsDamaged)
    {
        uint32_t next = (prevDelayMs == 0) ? MIN_RETRY_DELAY_MS : prevDelayMs * 2;
        return next > MAX_RETRY_DELAY_MS ? MAX_RETRY_DELAY_MS : next;
    }

    // A normal check actually ran - back to once a day.
    if (last == OtaStartGate::Outcome::Started) return STEADY_STATE_DELAY_MS;

    // The gate refused to start (most commonly NoWifi at boot+10s). Retry
    // soon rather than losing a full day to the next scheduled attempt.
    uint32_t next = (prevDelayMs == 0) ? MIN_RETRY_DELAY_MS : prevDelayMs * 2;
    return next > MAX_RETRY_DELAY_MS ? MAX_RETRY_DELAY_MS : next;
}

}  // namespace OtaRetrySchedule
