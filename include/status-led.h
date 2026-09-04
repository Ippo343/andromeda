#pragma once

#include "esp32-hal-rmt.h"

// "Board is powered" indicator: lights the onboard WS2812 a dim solid colour as
// the very first thing setup() does, so a bare board on a bench isn't a black
// rectangle you have to poke with a multimeter to trust.
//
// Only the two Waveshare "Zero" boards have a known onboard RGB LED, and their
// wiring does NOT match the DevKit board definitions we build against:
//   - esp32-s3-devkitc-1's RGB_BUILTIN is GPIO48; the S3-Zero's is GPIO21
//   - esp32-c3-devkitm-1's RGB_BUILTIN is GPIO8;  the C3-Zero's is GPIO10
// so the pin is hard-coded to the actual Zero hardware here rather than taken
// from the variant header. The classic WROOM build is deliberately skipped: its
// only candidate pin (GPIO2) is a strip output on AndromedaMk0.
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

inline void statusLedOn()
{
#ifdef STATUS_LED_PIN
    rmt_obj_t* rmt = rmtInit(STATUS_LED_PIN, RMT_TX_MODE, RMT_MEM_64);
    if (rmt == nullptr) { return; }
    rmtSetTick(rmt, 100);  // 100 ns per tick

    // Dim warm-white: unmistakably lit in a dark room, not a desk-blinding
    // beacon and barely any current draw. WS2812 wire order is G, R, B.
    const uint8_t color[3] = {5, 6, 3};

    rmt_data_t bits[24];
    int i = 0;
    for (int c = 0; c < 3; c++)
    {
        for (int bit = 7; bit >= 0; bit--)
        {
            const bool one = color[c] & (1 << bit);
            bits[i].level0 = 1;
            bits[i].duration0 = one ? 8 : 4;  // T1H 0.8us / T0H 0.4us
            bits[i].level1 = 0;
            bits[i].duration1 = one ? 4 : 8;  // T1L 0.4us / T0L 0.8us
            i++;
        }
    }
    rmtWriteBlocking(rmt, bits, 24);

    // Hand the channel back so FastLED can claim it at strip init - the pixel
    // holds the colour we just latched.
    rmtDeinit(rmt);
#endif
}
