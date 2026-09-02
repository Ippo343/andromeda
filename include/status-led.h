#pragma once

#include "esp32-hal-rmt.h"

// On-board status LED (#141): drives the onboard WS2812 to a dim solid colour
// reflecting the device state - Booting (the original "board has power" latch,
// set first thing in setup()), then WifiConnecting / WifiConnected / ApMode /
// Error / Running as those become known. statusLedSet() only writes on an
// actual state change, so it stays edge-triggered and never touches the RMT
// channel from the render loop. The state->colour table is the pure,
// natively-tested status-led-state.h.
//
// Only the two Waveshare "Zero" boards have a known onboard RGB LED, and their
// wiring does NOT match the DevKit board definitions we build against:
//   - esp32-s3-devkitc-1's RGB_BUILTIN is GPIO48; the S3-Zero's is GPIO21
//   - esp32-c3-devkitm-1's RGB_BUILTIN is GPIO8;  the C3-Zero's is GPIO10
// so the pin is hard-coded to the actual Zero hardware here rather than taken
// from the variant header. The classic WROOM build is deliberately skipped: its
// only candidate pin (GPIO2) is a strip output on AndromedaMk1.
//
// Bit-banged over one RMT TX channel that is released again (rmtDeinit) as soon
// as the 24 bits are out. The WS2812 latches its last value, so the LED stays
// lit for the whole session with no refresh - and, critically, the channel is
// free by the time GEOMETRY.initialize() binds FastLED's own RMT controller.
// arduino-esp32's neopixelWrite() would do the same job in one call, but it
// keeps its RMT channel forever: on the ESP32-C3 (only ~2 usable channels, and
// RMT_MEM_64 grabs the shared block) that starves FastLED and the first
// FastLED.show() hangs. On the roomier S3 the leak is harmless, but there's no
// reason to special-case it.

#if defined(ESP32_S3)
#define STATUS_LED_PIN 21
#elif defined(ESP32_C3)
#define STATUS_LED_PIN 10
#endif

#include "status-led-state.h"

#ifdef STATUS_LED_PIN
// One 24-bit WS2812 frame: init a channel, blast the bits, hand it straight
// back (rmtDeinit) so FastLED can claim it at strip init. The pixel latches
// the last value, so it holds the colour with no refresh. Blocking, but every
// caller is either setup() or an edge-triggered state change - never the
// render loop (see statusLedSet()).
inline void statusLedWriteFrame(StatusLed::LedColor c)
{
    rmt_obj_t* rmt = rmtInit(STATUS_LED_PIN, RMT_TX_MODE, RMT_MEM_64);
    if (rmt == nullptr) { return; }
    rmtSetTick(rmt, 100);  // 100 ns per tick

    const uint8_t bytes[3] = {c.g, c.r, c.b};  // WS2812 wire order
    rmt_data_t bits[24];
    int i = 0;
    for (int byteIdx = 0; byteIdx < 3; byteIdx++)
    {
        for (int bit = 7; bit >= 0; bit--)
        {
            const bool one = bytes[byteIdx] & (1 << bit);
            bits[i].level0 = 1;
            bits[i].duration0 = one ? 8 : 4;  // T1H 0.8us / T0H 0.4us
            bits[i].level1 = 0;
            bits[i].duration1 = one ? 4 : 8;  // T1L 0.4us / T0L 0.8us
            i++;
        }
    }
    rmtWriteBlocking(rmt, bits, 24);
    rmtDeinit(rmt);
}
#endif

// Drive the on-board LED to reflect a device state. A no-op on boards with no
// known onboard LED (the WROOM - STATUS_LED_PIN undefined). Only performs the
// RMT write when the state actually changed, so it's safe to call from
// edge-y places (WiFi callbacks, mode changes) or, in principle, per frame.
inline void statusLedSet(StatusLed::DeviceState state)
{
#ifdef STATUS_LED_PIN
    static StatusLed::DeviceState last = StatusLed::DeviceState::_Count;  // force the first write
    if (!StatusLed::shouldWrite(last, state)) return;
    last = state;
    statusLedWriteFrame(StatusLed::colorFor(state));
#else
    (void)state;
#endif
}

// Back-compat name for main.cpp's first-thing-in-setup() call: "the board has
// power" is the Booting state.
inline void statusLedOn() { statusLedSet(StatusLed::DeviceState::Booting); }
