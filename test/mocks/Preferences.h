#pragma once

#include <cstdint>
#include <iterator>
#include <map>
#include <set>
#include <string>

#include "WString.h"

// Minimal stand-in for the ESP32 Arduino core's Preferences.h (NVS).
// Backed by in-memory, namespace-scoped maps (not real NVS persistence - it
// resets on process exit, which is exactly what native tests want) so
// FactoryConfig (geometry.cpp) and Comms' WiFi credential storage actually
// round-trip natively instead of silently no-op'ing.
//
// begin(name, readOnly) mirrors real NVS's lazy-namespace-creation behavior:
// a write-mode open always succeeds and creates the namespace (real
// nvs_open() does this too - a namespace is nothing but an implicit mapping
// created the first time it's opened for write), but a *readonly* open on a
// namespace that has never been opened for write fails, matching
// ESP_ERR_NVS_NOT_FOUND. This is what FactoryConfig::getModelId() (#162's
// L10_MK1-default work) depends on to detect "never configured" and
// self-persist a default on first access.

class Preferences
{
   public:
    bool begin(const char* name, bool readOnly = false)
    {
        ns = name;
        if (!readOnly)
        {
            writtenNamespaces().insert(ns);
            return true;
        }
        return writtenNamespaces().count(ns) > 0;
    }
    void end() {}

    bool isKey(const char* key)
    {
        const std::string fullKey = ns + "/" + key;
        return ushortStore().count(fullKey) || uintStore().count(fullKey) ||
               stringStore().count(fullKey);
    }

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

    // Test-only: wipes every namespace back to "never opened", not just the
    // key/value data - clear() alone (like real NVS's nvs_erase_all()) does
    // NOT undo a namespace's existence, so it can't be used to simulate a
    // truly virgin device. Needed by any test exercising the "never
    // configured" self-persist path (FactoryConfig::getModelId()) in
    // isolation from whatever an earlier test in the same binary wrote.
    static void resetAllForTests()
    {
        ushortStore().clear();
        uintStore().clear();
        stringStore().clear();
        writtenNamespaces().clear();
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
    static std::set<std::string>& writtenNamespaces()
    {
        static std::set<std::string> s;
        return s;
    }
};
