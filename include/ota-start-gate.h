#pragma once

#include <cstdint>

// Pure entry-gate logic for OtaUpdater::startCheck() / startUpdate() (#63).
// Kept here (no Arduino, no FreeRTOS) so the native suite
// (test/test_ota_start_gate) covers the branch that decides whether a worker
// is actually spawned - the caller in src/comms.cpp maps the outcome to an
// HTTP status so the Advanced page never shows "started" for a request that
// silently did nothing.

namespace OtaStartGate
{

enum class Outcome : uint8_t
{
    Started,  // preconditions met, single-flight slot claimed - worker spawned
    Busy,     // a check/update task is already running
    NoWifi,   // not connected in station mode
    LowHeap,  // not enough free heap to risk a TLS + Update allocation
};

// `wifiConnectedSta` == connected AND in a mode that includes STA.
// `freeHeap` / `minFreeHeap` are ESP.getFreeHeap() and the MIN_FREE_HEAP
// floor. `taskInFlight` is the single-flight flag, sampled under the lock.
//
// Order matters: report the most fundamental blocker first so the HTTP status
// is the useful one (no point saying "busy" when WiFi is down anyway).
inline Outcome evaluate(bool wifiConnectedSta, uint32_t freeHeap, uint32_t minFreeHeap,
                        bool taskInFlight)
{
    if (!wifiConnectedSta) return Outcome::NoWifi;
    if (freeHeap < minFreeHeap) return Outcome::LowHeap;
    if (taskInFlight) return Outcome::Busy;
    return Outcome::Started;
}

// HTTP status for each outcome: 202 accepted, 409 conflict (retry later),
// 503 unavailable (fix the precondition first).
inline int httpStatus(Outcome o)
{
    switch (o)
    {
        case Outcome::Started:
            return 202;
        case Outcome::Busy:
            return 409;
        case Outcome::NoWifi:
        case Outcome::LowHeap:
            return 503;
    }
    return 500;
}

// A short human reason for the response body / the Advanced page.
inline const char* message(Outcome o)
{
    switch (o)
    {
        case Outcome::Started:
            return "started";
        case Outcome::Busy:
            return "an OTA task is already running";
        case Outcome::NoWifi:
            return "WiFi not connected";
        case Outcome::LowHeap:
            return "not enough free memory right now";
    }
    return "unknown";
}

}  // namespace OtaStartGate
