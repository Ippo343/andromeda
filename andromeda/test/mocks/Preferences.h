#pragma once

#include <cstdint>
#include <map>
#include <string>

// Minimal stand-in for the ESP32 Arduino core's Preferences.h (NVS).
// Backed by an in-memory, namespace-scoped map (not real NVS persistence -
// it resets on process exit, which is exactly what native tests want) so
// FactoryConfig (geometry.cpp) and Comms' WiFi credential storage actually
// round-trip natively instead of silently no-op'ing.

class Preferences
{
   public:
    bool begin(const char* name, bool = false)
    {
        ns = name;
        return true;
    }
    void end() {}

    size_t putUShort(const char* key, uint16_t value)
    {
        store()[ns + "/" + key] = value;
        return sizeof(uint16_t);
    }
    uint16_t getUShort(const char* key, uint16_t defaultValue = 0)
    {
        auto it = store().find(ns + "/" + key);
        return it != store().end() ? it->second : defaultValue;
    }

   private:
    std::string ns;

    // Function-local static: avoids static-init-order issues across
    // translation units, shared by every Preferences instance/namespace.
    static std::map<std::string, uint16_t>& store()
    {
        static std::map<std::string, uint16_t> s;
        return s;
    }
};
