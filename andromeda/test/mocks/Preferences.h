#pragma once

#include <cstdint>

// Minimal no-op stand-in for the ESP32 Arduino core's Preferences.h (NVS).
// FactoryConfig (geometry.cpp) is explicitly out of scope for native tests
// (see testing plan Part 1) - this exists only so the rest of geometry.cpp,
// which shares a translation unit with FactoryConfig, compiles natively.

class Preferences
{
   public:
    bool begin(const char*, bool = false) { return true; }
    void end() {}
    size_t putUShort(const char*, uint16_t) { return sizeof(uint16_t); }
    uint16_t getUShort(const char*, uint16_t defaultValue = 0) { return defaultValue; }
};
