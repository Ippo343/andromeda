#pragma once

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
// only candidate pin (GPIO2) is a strip output on AndromedaMk1.
//
// Driven via arduino-esp32's neopixelWrite() (bit-banged over RMT) instead of
// FastLED: this runs before GEOMETRY.initialize() has bound any FastLED
// controller, and C3's GPIO10 is on FastLED's forbidden list anyway. WS2812s
// latch their last value, so a single call keeps the LED lit for the whole
// session - no refresh needed. If a model's strip happens to use this pin
// (L70_MK1 on GPIO21), FastLED simply takes the pin over at strip init and the
// onboard LED becomes that strip's pixel 0.

#if defined(ESP32_S3)
#define STATUS_LED_PIN 21
#elif defined(ESP32_C3)
#define STATUS_LED_PIN 10
#endif

inline void statusLedOn()
{
#ifdef STATUS_LED_PIN
    // Dim warm-white: unmistakably lit in a dark room, not a desk-blinding
    // beacon and barely any current draw.
    neopixelWrite(STATUS_LED_PIN, 6, 5, 3);
#endif
}
