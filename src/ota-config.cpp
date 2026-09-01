#include "ota-config.h"

#include <Preferences.h>

#include <cstring>

#include "nvs-utils.h"

namespace OtaConfig
{
static const char* PREFS_NAMESPACE = "device";
static const char* DEV_CHANNEL_KEY = "ota_dev_ch";
static const char* LAST_CODE_KEY = "ota_last";
static const char* FS_MD5_KEY = "ota_fs_md5";

void persistDevChannel(bool dev)
{
    // Same NVS-wear dedupe as BrightnessConfig::persist(): the toggle is a
    // deliberate user click, but it comes off an unauthenticated route.
    static int lastPersisted = -1;
    int value = dev ? 1 : 0;
    if (lastPersisted == value) return;

    Preferences prefs;
    if (!beginPreferencesOrWarn(prefs, PREFS_NAMESPACE, false)) return;
    putUShortOrWarn(prefs, DEV_CHANNEL_KEY, static_cast<uint16_t>(value));
    prefs.end();

    lastPersisted = value;
    Log.noticeln("Persisted OTA channel: %s", dev ? "dev" : "stable");
}

bool devChannel()
{
    Preferences prefs;
    if (!beginPreferencesOrWarn(prefs, PREFS_NAMESPACE, true)) return false;
    uint16_t value = prefs.getUShort(DEV_CHANNEL_KEY, 0);
    prefs.end();
    return value != 0;
}

void persistApplied(uint32_t versionCode, const char* fsMd5)
{
    Preferences prefs;
    if (!beginPreferencesOrWarn(prefs, PREFS_NAMESPACE, false)) return;
    if (prefs.putUInt(LAST_CODE_KEY, versionCode) == 0)
        Log.errorln("Preferences: failed to write \"%s\"", LAST_CODE_KEY);
    putStringOrWarn(prefs, FS_MD5_KEY, String(fsMd5 ? fsMd5 : ""));
    prefs.end();

    Log.noticeln("Persisted applied OTA version code: %u", versionCode);
}

uint32_t lastAppliedCode()
{
    Preferences prefs;
    if (!beginPreferencesOrWarn(prefs, PREFS_NAMESPACE, true)) return 0;
    uint32_t value = prefs.getUInt(LAST_CODE_KEY, 0);
    prefs.end();
    return value;
}

bool appliedFsMd5(char* out, size_t outCap)
{
    if (out == nullptr || outCap == 0) return false;

    Preferences prefs;
    if (!beginPreferencesOrWarn(prefs, PREFS_NAMESPACE, true)) return false;
    String stored = prefs.getString(FS_MD5_KEY, String());
    prefs.end();

    if (stored.length() == 0 || stored.length() >= outCap) return false;
    std::memcpy(out, stored.c_str(), stored.length() + 1);
    return true;
}

}  // namespace OtaConfig
