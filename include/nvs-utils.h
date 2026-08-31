#pragma once

#include <ArduinoLog.h>
#include <Preferences.h>

// Thin wrapper around Preferences::begin()/put*() that logs on failure instead of silently
// continuing - every production Preferences call site in the firmware previously ignored
// these return values, so a full or corrupt NVS namespace made the device quietly "forget"
// WiFi credentials, its model ID, its name, or its brightness with nothing logged to explain
// why. (A fully corrupt NVS *partition* is already recovered by the Arduino core's own
// nvs_flash_init() erase-and-retry at boot, before setup() ever runs - see
// esp32-hal-misc.c - so there's no equivalent recovery to add here at the application level;
// this is strictly about making an unexpected failure visible in the log.)

inline bool beginPreferencesOrWarn(Preferences& prefs, const char* nsName, bool readOnly)
{
    bool ok = prefs.begin(nsName, readOnly);
    if (!ok) Log.errorln("Preferences::begin(\"%s\") failed - NVS namespace unavailable", nsName);
    return ok;
}

inline void putUShortOrWarn(Preferences& prefs, const char* key, uint16_t value)
{
    if (prefs.putUShort(key, value) == 0) Log.errorln("Preferences: failed to write \"%s\"", key);
}

inline void putStringOrWarn(Preferences& prefs, const char* key, const String& value)
{
    // putString() returns the number of bytes written, which is legitimately 0 for an empty
    // string - only flag it as a failure when the value itself wasn't empty.
    if (prefs.putString(key, value) == 0 && value.length() > 0)
        Log.errorln("Preferences: failed to write \"%s\"", key);
}
