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
static const char* PARTIAL_FAIL_KEY = "ota_pfail";
static const char* PARTIAL_REASON_KEY = "ota_pfail_r";

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
    // A clean apply - a real filesystem md5, not the "" the firmware-only
    // degraded path writes - means the FS half took, so any partial-failure
    // flag from an earlier attempt no longer applies.
    if (fsMd5 && fsMd5[0] != '\0')
    {
        prefs.remove(PARTIAL_FAIL_KEY);
        prefs.remove(PARTIAL_REASON_KEY);
    }
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

void persistPartialFailure(const char* reason)
{
    Preferences prefs;
    if (!beginPreferencesOrWarn(prefs, PREFS_NAMESPACE, false)) return;
    putUShortOrWarn(prefs, PARTIAL_FAIL_KEY, 1);
    putStringOrWarn(prefs, PARTIAL_REASON_KEY, String(reason ? reason : ""));
    prefs.end();
    Log.errorln("OTA: recorded a partial failure for the next boot to report: %s",
                reason ? reason : "");
}

void clearPartialFailure()
{
    Preferences prefs;
    if (!beginPreferencesOrWarn(prefs, PREFS_NAMESPACE, false)) return;
    prefs.remove(PARTIAL_FAIL_KEY);
    prefs.remove(PARTIAL_REASON_KEY);
    prefs.end();
}

bool partialFailurePending()
{
    Preferences prefs;
    if (!beginPreferencesOrWarn(prefs, PREFS_NAMESPACE, true)) return false;
    uint16_t value = prefs.getUShort(PARTIAL_FAIL_KEY, 0);
    prefs.end();
    return value != 0;
}

bool partialFailureReason(char* out, size_t outCap)
{
    if (out == nullptr || outCap == 0) return false;
    out[0] = '\0';

    Preferences prefs;
    if (!beginPreferencesOrWarn(prefs, PREFS_NAMESPACE, true)) return false;
    uint16_t pending = prefs.getUShort(PARTIAL_FAIL_KEY, 0);
    String stored = prefs.getString(PARTIAL_REASON_KEY, String());
    prefs.end();

    if (pending == 0 || stored.length() >= outCap) return false;
    std::memcpy(out, stored.c_str(), stored.length() + 1);
    return true;
}

}  // namespace OtaConfig
