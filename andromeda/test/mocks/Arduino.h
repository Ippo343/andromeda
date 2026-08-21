#pragma once

// Minimal Arduino API surface for native (host) unit tests.
// Only implements what this project's non-hardware code actually calls
// (utils.h/.cpp, effects, mission-control, etc.) - not a general Arduino shim.

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>

#define ARDUINO 10819

#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif

using std::abs;
using std::max;
using std::min;

// Real Arduino's constrain() is a macro (not a same-type-only template), so
// it accepts mixed argument types (e.g. constrain(someFloat, 0, 255)) via
// normal C promotion rules - match that here, since project code relies on it.
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

inline long map(long x, long in_min, long in_max, long out_min, long out_max)
{
    const long run = in_max - in_min;
    if (run == 0) return 0;
    const long rise = out_max - out_min;
    return (x - in_min) * rise / run + out_min;
}

// millis()/delay() are provided by FastLED's own native stub platform
// (platforms/stub/led_sysdefs_stub_generic.h) - declaring them again here
// would conflict, so this mock leaves them to FastLED.

// A dedicated PRNG (rather than std::rand(), whose RAND_MAX can be as small
// as 32767 on some toolchains) so that wide random(min, max) ranges are
// actually explored across their full span.
inline std::mt19937& randomEngine()
{
    static std::mt19937 engine(0);
    return engine;
}

inline void randomSeed(unsigned long seed) { randomEngine().seed(static_cast<unsigned int>(seed)); }

// Arduino semantics: random(max) and random(min,max) are exclusive of max.
inline long random(long min, long max)
{
    if (max <= min) return min;
    std::uniform_int_distribution<long> dist(min, max - 1);
    return dist(randomEngine());
}

inline long random(long max) { return random(0L, max); }

inline int analogRead(int) { return std::rand() % 1024; }

// PROGMEM: ESP32's Arduino core maps these to plain memory access (no
// separate flash address space like AVR), so on the host they're just
// direct dereferences. FastLED's own null_progmem.h (included when
// FASTLED_USE_PROGMEM isn't 1, which holds for our native/stub build)
// already defines the bare PROGMEM keyword as empty; this project's code
// additionally calls the raw pgm_read_*/memcpy_P names directly (as on
// ESP32), which FastLED doesn't provide, so those are added here.
#include <cstring>

#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*reinterpret_cast<const uint8_t*>(addr))
#endif
#ifndef pgm_read_word
#define pgm_read_word(addr) (*reinterpret_cast<const uint16_t*>(addr))
#endif
#ifndef pgm_read_ptr
#define pgm_read_ptr(addr) (*reinterpret_cast<const void* const*>(addr))
#endif
#ifndef memcpy_P
#define memcpy_P(dest, src, n) std::memcpy(dest, src, n)
#endif

// ESP32 Arduino core HAL function - no-op on the host (there is no real CPU
// frequency to scale).
inline void setCpuFrequencyMhz(uint32_t) {}

// ESP-IDF function pulled in by MissionControl::processWebCommands() (REBOOT
// command) - no-op on the host, there is nothing to restart.
inline void esp_restart() {}
