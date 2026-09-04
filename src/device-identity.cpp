#include "device-identity.h"

#include <cstring>

#include "geometry/geometry.h"
#include "nvs-utils.h"

namespace DeviceIdentity
{

namespace
{
char cachedUid[DeviceUid::LENGTH + 1] = {0};
}  // namespace

const char* getUid()
{
    if (cachedUid[0] == '\0')
    {
        uint8_t mac[6] = {0};
        WiFi.macAddress(mac);
        DeviceUid::generate(mac, cachedUid);
    }
    return cachedUid;
}

String getDefaultName()
{
    // Reads the *running* model (GEOMETRY, already in RAM) rather than
    // FactoryConfig::getModelId() (NVS): the SSID/hostname are only ever
    // applied at boot (Comms::setup()), so the name should track what's
    // actually running, not a model change queued for next boot - and this
    // is called on every WS state push (Comms::buildCurrentStateJson()), so
    // it must not cost an NVS read.
    const ModelConfig* config = GEOMETRY.getConfig();
    if (!config || config->name[0] == '\0') return String("Andromeda-") + getUid();

    // First space-delimited token of the display name, e.g. "L10 MK1" ->
    // "L10", "Andromeda MK0" -> "Andromeda" (preserving today's default for
    // that one model). Sanitized the same way a user-supplied name is.
    const char* space = strchr(config->name, ' ');
    size_t tokenLen = space ? (size_t)(space - config->name) : strlen(config->name);
    char prefix[DeviceUid::MAX_NAME_LENGTH + 1];
    size_t copyLen = tokenLen < sizeof(prefix) - 1 ? tokenLen : sizeof(prefix) - 1;
    memcpy(prefix, config->name, copyLen);
    prefix[copyLen] = '\0';

    char sanitized[DeviceUid::MAX_NAME_LENGTH + 1];
    DeviceUid::sanitize(prefix, sanitized, sizeof(sanitized));
    if (sanitized[0] == '\0') return String("Andromeda-") + getUid();

    return String(sanitized) + "-" + getUid();
}

String getCustomName()
{
    Preferences prefs;
    if (!beginPreferencesOrWarn(prefs, PREFS_NAMESPACE, true)) return "";
    // isKey() first: a device that's never had a custom name set has no
    // NAME_KEY at all, and calling getString() on a missing key logs an
    // ESP-IDF-level ERROR for the miss (nvs_get_str, independent of and
    // beneath the Arduino Preferences wrapper's own graceful default-value
    // handling) - harmless, but this runs on every WS state keepalive
    // (Comms::buildCurrentStateJson(), every STATE_KEEPALIVE_INTERVAL_MS),
    // so a never-renamed device spammed that line forever.
    String name = prefs.isKey(NAME_KEY) ? prefs.getString(NAME_KEY, "") : "";
    prefs.end();
    return name;
}

namespace
{
// Shared by getMdnsHostname()/getDefaultMdnsHostname()/getAndromedaMdnsHostname(): copies
// `name` into a fixed buffer (truncating like the rest of this file does) and lowercases it
// in place.
String toMdnsLabel(const String& name)
{
    char buf[DeviceUid::MAX_NAME_LENGTH + 1];
    size_t i = 0;
    for (; i < sizeof(buf) - 1 && name.c_str()[i] != '\0'; i++) buf[i] = name.c_str()[i];
    buf[i] = '\0';
    DeviceUid::toLowerAscii(buf);
    return String(buf);
}
}  // namespace

String getMdnsHostname() { return toMdnsLabel(getDeviceName()); }

String getDefaultMdnsHostname() { return toMdnsLabel(getDefaultName()); }

String getAndromedaMdnsHostname() { return toMdnsLabel(String("Andromeda-") + getUid()); }

void setDeviceName(const String& name)
{
    char sanitized[DeviceUid::MAX_NAME_LENGTH + 1];
    DeviceUid::sanitize(name.c_str(), sanitized, sizeof(sanitized));

    Preferences prefs;
    if (!beginPreferencesOrWarn(prefs, PREFS_NAMESPACE, false)) return;
    if (sanitized[0] == '\0')
        prefs.remove(NAME_KEY);
    else
        putStringOrWarn(prefs, NAME_KEY, sanitized);
    prefs.end();
}

}  // namespace DeviceIdentity
