#include "device-identity.h"

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

String getDefaultName() { return String("Andromeda-") + getUid(); }

String getCustomName()
{
    Preferences prefs;
    if (!beginPreferencesOrWarn(prefs, PREFS_NAMESPACE, true)) return "";
    String name = prefs.getString(NAME_KEY, "");
    prefs.end();
    return name;
}

String getMdnsHostname()
{
    String name = getDeviceName();
    char buf[DeviceUid::MAX_NAME_LENGTH + 1];
    size_t i = 0;
    for (; i < sizeof(buf) - 1 && name.c_str()[i] != '\0'; i++) buf[i] = name.c_str()[i];
    buf[i] = '\0';
    DeviceUid::toLowerAscii(buf);
    return String(buf);
}

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
