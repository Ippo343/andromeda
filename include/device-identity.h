#pragma once

#include <Preferences.h>
#include <WiFi.h>

#include "device-uid.h"

// Owns this device's identity on the network: a UID derived from the
// ESP32's MAC address, combined with the running model's name into a default
// AP SSID/mDNS hostname (e.g. "L70-$UID"; "Andromeda-$UID" for ANDROMEDA_MK0
// and as the fallback if the model is ever unresolvable - see
// getDefaultName()), and an optional user-chosen name that overrides the
// default entirely. A rename only takes effect after reboot - Comms::setup()
// reads the name once, at startAPMode()/startStationMode() time - mirroring
// FactoryConfig's model-ID reboot-required pattern in geometry.cpp.
namespace DeviceIdentity
{

constexpr const char* PREFS_NAMESPACE = "device";
constexpr const char* NAME_KEY = "dev_name";

// The UID derived from this device's MAC address (e.g. "A1B2"). Computed
// once and cached for the process lifetime.
const char* getUid();

// The name used when the user has never set a custom one: derived from the
// running model's name plus the UID (e.g. "L70-$UID"), or "Andromeda-$UID"
// for ANDROMEDA_MK0 and as the fallback if the model can't be resolved.
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

// Lowercased form of getDefaultName(), regardless of whether a custom name
// is currently set - the "<model-token>-<uid>" mDNS label that survives any
// rename (e.g. "l70-a1b2"). Comms::startMdns() registers this as a delegated
// hostname alongside the primary (see include/mdns-hosts.h), so the device
// stays reachable at its factory name even after the custom one changes.
String getDefaultMdnsHostname();

// Lowercased, UID-free model token (e.g. "l70"), or "andromeda" if the running model is
// unresolvable - the bare-model half of the mDNS host pair Comms::startMdns() falls back to
// when "<model-token>-<uid>" (getDefaultMdnsHostname()) turns out to be taken (see
// include/mdns-hosts.h).
String getModelMdnsHostname();

// Lowercased "andromeda-<uid>" mDNS label - model-independent, so
// docs/stickers can give one formula for every board regardless of which
// model it is ("your device answers to andromeda-" + the 4-character UID on
// its label). Also registered as a delegated hostname by Comms::startMdns().
String getAndromedaMdnsHostname();

// Persists a new device name (NVS), sanitized to safe SSID/hostname
// characters (see DeviceUid::sanitize) and truncated to
// DeviceUid::MAX_NAME_LENGTH. Passing an empty/all-invalid name clears back
// to the default. Takes effect after reboot.
void setDeviceName(const String& name);

}  // namespace DeviceIdentity
