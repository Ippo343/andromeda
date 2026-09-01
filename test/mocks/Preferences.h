#pragma once

#include <cstdint>
#include <iterator>
#include <map>
#include <string>

#include "WString.h"

// Minimal stand-in for the ESP32 Arduino core's Preferences.h (NVS).
// Backed by in-memory, namespace-scoped maps (not real NVS persistence - it
// resets on process exit, which is exactly what native tests want) so
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
        ushortStore()[ns + "/" + key] = value;
        return sizeof(uint16_t);
    }
    uint16_t getUShort(const char* key, uint16_t defaultValue = 0)
    {
        auto it = ushortStore().find(ns + "/" + key);
        return it != ushortStore().end() ? it->second : defaultValue;
    }

    size_t putUInt(const char* key, uint32_t value)
    {
        uintStore()[ns + "/" + key] = value;
        return sizeof(uint32_t);
    }
    uint32_t getUInt(const char* key, uint32_t defaultValue = 0)
    {
        auto it = uintStore().find(ns + "/" + key);
        return it != uintStore().end() ? it->second : defaultValue;
    }

    size_t putString(const char* key, const String& value)
    {
        stringStore()[ns + "/" + key] = value.c_str();
        return value.length();
    }
    String getString(const char* key, const String& defaultValue = String())
    {
        auto it = stringStore().find(ns + "/" + key);
        return it != stringStore().end() ? String(it->second) : defaultValue;
    }

    void clear()
    {
        const std::string prefix = ns + "/";
        for (auto it = ushortStore().begin(); it != ushortStore().end();)
        {
            it = (it->first.rfind(prefix, 0) == 0) ? ushortStore().erase(it) : std::next(it);
        }
        for (auto it = uintStore().begin(); it != uintStore().end();)
        {
            it = (it->first.rfind(prefix, 0) == 0) ? uintStore().erase(it) : std::next(it);
        }
        for (auto it = stringStore().begin(); it != stringStore().end();)
        {
            it = (it->first.rfind(prefix, 0) == 0) ? stringStore().erase(it) : std::next(it);
        }
    }

    bool remove(const char* key)
    {
        const std::string fullKey = ns + "/" + key;
        bool removed = false;
        removed |= ushortStore().erase(fullKey) > 0;
        removed |= uintStore().erase(fullKey) > 0;
        removed |= stringStore().erase(fullKey) > 0;
        return removed;
    }

   private:
    std::string ns;

    // Function-local statics: avoid static-init-order issues across
    // translation units, shared by every Preferences instance/namespace.
    static std::map<std::string, uint16_t>& ushortStore()
    {
        static std::map<std::string, uint16_t> s;
        return s;
    }
    static std::map<std::string, uint32_t>& uintStore()
    {
        static std::map<std::string, uint32_t> s;
        return s;
    }
    static std::map<std::string, std::string>& stringStore()
    {
        static std::map<std::string, std::string> s;
        return s;
    }
};
