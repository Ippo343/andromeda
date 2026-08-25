#pragma once

#include <cstdlib>
#include <cstring>

#include "mission-control.h"

// Pure, dependency-free (no Arduino String, no ArduinoJson) parser for the
// hand-rolled JSON messages the WS client sends. Deliberately free of
// Arduino/WiFi includes so it's natively unit-testable, mirroring
// comms-utils.h's isScanCacheValid extraction (comms.cpp itself stays
// excluded from native builds - see platformio.ini [env:native]).

namespace WsCommandParser
{

// `json` must be NUL-terminated (matches AsyncWebSocket's `data[len] = 0`
// convention at the call site). Returns true and fills `out` on a
// recognized, well-formed command message; false otherwise (unknown type,
// missing field, or out-of-range field).
inline bool findField(const char* json, const char* key, long& outValue)
{
    const char* p = strstr(json, key);
    if (!p) return false;
    outValue = strtol(p + strlen(key), nullptr, 10);
    return true;
}

inline bool parse(const char* json, Command& out)
{
    if (strstr(json, "\"type\":\"next\""))
    {
        out = Command::Next();
        return true;
    }
    if (strstr(json, "\"type\":\"hold\""))
    {
        out = Command::Hold();
        return true;
    }
    if (strstr(json, "\"type\":\"resume\""))
    {
        out = Command::Resume();
        return true;
    }
    if (strstr(json, "\"type\":\"power_off\""))
    {
        out = Command::PowerOff();
        return true;
    }
    if (strstr(json, "\"type\":\"power_on\""))
    {
        out = Command::PowerOn();
        return true;
    }
    if (strstr(json, "\"type\":\"reboot\""))
    {
        out = Command::Reboot();
        return true;
    }

    if (strstr(json, "\"type\":\"color\""))
    {
        long r, g, b;
        if (!findField(json, "\"r\":", r) || !findField(json, "\"g\":", g) ||
            !findField(json, "\"b\":", b))
            return false;
        if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) return false;
        out = Command::Color(static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                             static_cast<uint8_t>(b));
        return true;
    }

    if (strstr(json, "\"type\":\"model\""))
    {
        long id;
        if (!findField(json, "\"id\":", id)) return false;
        if (id < 0) return false;
        out = Command::Model(static_cast<uint16_t>(id));
        return true;
    }

    if (strstr(json, "\"type\":\"effect\""))
    {
        long id;
        if (!findField(json, "\"id\":", id)) return false;
        if (id < 0 || id > 255) return false;
        out = Command::Effect(static_cast<uint8_t>(id));
        return true;
    }

    return false;
}

// Fast-path parser for BRIGHTNESS: applied directly from the network task
// (setMaxBrightness() re-read every frame by the render loop) rather than
// carried through Command/CommandType and the render-task command queue -
// see the queue-overrun/watchdog note on CommandType in mission-control.h.
// outCommit is true only for the one message sent when the slider drag ends
// (see controls.js's stopSliderDrag) - that's the signal to persist the
// value to NVS, rather than doing so on every drag-speed update.
inline bool parseBrightness(const char* json, uint8_t& outValue, bool& outCommit)
{
    if (!strstr(json, "\"type\":\"brightness\"")) return false;
    long value;
    if (!findField(json, "\"value\":", value)) return false;
    if (value < 0 || value > 255) return false;
    outValue = static_cast<uint8_t>(value);
    outCommit = strstr(json, "\"commit\":true") != nullptr;
    return true;
}

}  // namespace WsCommandParser
