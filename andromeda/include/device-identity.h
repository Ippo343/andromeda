#pragma once

#include <Preferences.h>
#include <WiFi.h>

#include "device-uid.h"

// Owns this device's identity on the network: a UID derived from the
// ESP32's MAC address (the default AP SSID "Andromeda-$UID" and mDNS
// hostname "andromeda-$uid.local" - see wifi-esp-adapters.cpp/comms.cpp) and
// an optional user-chosen name that overrides the default entirely. A
// rename only takes effect after reboot - Comms::setup() reads the name
// once, at startAPMode()/startStationMode() time - mirroring FactoryConfig's
// model-ID reboot-required pattern in geometry.cpp.
namespace DeviceIdentity
{

constexpr const char* PREFS_NAMESPACE = "device";
constexpr const char* NAME_KEY = "dev_name";

// The UID derived from this device's MAC address (e.g. "A1B2"). Computed
// once and cached for the process lifetime.
const char* getUid();

// "Andromeda-$UID" - the name used when the user has never set a custom one.
String getDefaultName();

// The name currently persisted by the user, or "" if never customized.
String getCustomName();

// True if the user has set a custom device name.
inline bool isNameCustomized() { return getCustomName().length() > 0; }

// The name to actually use: the custom name if set, otherwise the default.
inline String getDeviceName()
{
    String custom = getCustomName();
    return custom.length() > 0 ? custom : getDefaultName();
}

// Lowercased form of getDeviceName(), for use as the mDNS hostname label
// (SSIDs preserve case; mDNS hostnames are conventionally lowercase).
String getMdnsHostname();

// Persists a new device name (NVS), sanitized to safe SSID/hostname
// characters (see DeviceUid::sanitize) and truncated to
// DeviceUid::MAX_NAME_LENGTH. Passing an empty/all-invalid name clears back
// to the default. Takes effect after reboot.
void setDeviceName(const String& name);

}  // namespace DeviceIdentity
