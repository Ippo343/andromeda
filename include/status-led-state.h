#pragma once

#include <cstdint>

// Pure state -> colour mapping for the on-board status LED (#141). No Arduino
// / RMT here so the native suite covers it; the bit-banging that actually
// drives the WS2812 lives in status-led.h behind #ifdef STATUS_LED_PIN, the
// same split as comms.cpp vs comms-utils.h.

namespace StatusLed
{

// The device states worth signalling on a single dim pixel. Ordered rough
// boot -> running; a value outside the enum maps to the Error colour rather
// than reading past the table (see colorFor()).
enum class DeviceState : uint8_t
{
    Booting,         // powered, setup() running - the "board has power" latch
    WifiConnecting,  // trying stored credentials
    WifiConnected,   // joined a network
    ApMode,          // fell back to / started the setup access point
    Error,           // stored credentials failed, or an unrecoverable fault
    Running,         // normal FX_LOOP operation
    _Count,
};

// WS2812 wire order is green, red, blue (see status-led.h). Values are
// deliberately tiny - this is an indicator, not illumination.
struct LedColor
{
    uint8_t g, r, b;

    bool operator==(const LedColor& o) const { return g == o.g && r == o.r && b == o.b; }
    bool operator!=(const LedColor& o) const { return !(*this == o); }
};

// Colour for a state. Distinct per state so the enum can't silently collide
// (test_status_led asserts pairwise inequality). An out-of-range cast falls
// back to the Error colour instead of indexing past the table.
inline LedColor colorFor(DeviceState s)
{
    switch (s)
    {
        case DeviceState::Booting:
            return {5, 6, 3};  // dim warm white - matches the old statusLedOn()
        case DeviceState::WifiConnecting:
            return {6, 5, 0};  // amber
        case DeviceState::WifiConnected:
            return {8, 0, 0};  // green
        case DeviceState::ApMode:
            return {0, 0, 10};  // blue
        case DeviceState::Running:
            return {2, 2, 2};  // faint white - "up and idle", less bright than Booting
        case DeviceState::Error:
        case DeviceState::_Count:
        default:
            return {0, 10, 0};  // red
    }
}

// Whether a transition from `last` to `next` needs an actual RMT write. Only
// when the state changed - so calling statusLedSet() every FX_LOOP frame (if a
// caller ever did) costs one comparison, not a 24-bit blocking bit-bang.
inline bool shouldWrite(DeviceState last, DeviceState next) { return last != next; }

}  // namespace StatusLed
