#pragma once

#include <atomic>

// Set by setup() in main.cpp when LittleFS could not mount its partition and
// had to be reformatted empty. The device is up (logging and every
// NVS-backed / route-only service still work), but it has no web assets - the
// most likely cause is an OTA (#63) that lost power part-way through the raw
// filesystem write.
//
// OtaUpdater reads this so the update path will re-fetch *this* firmware
// version's filesystem image, which the normal `latest > running` gate would
// otherwise refuse as "already up to date", stranding the device with no web
// UI and no in-band way to repair it. See include/ota-eligibility.h.
inline volatile bool g_fsDamaged = false;

// Boot-lifetime flag for the other end of the same OTA filesystem write: OtaUpdater's
// updateTask() calls LittleFS.end() to raw-flash a new filesystem image (src/ota-updater.cpp),
// and nothing stopped comms.cpp's static-file/log routes from still touching LittleFS during
// that 10-30s unmounted window - a use-after-unmount in the VFS layer. markFsUnmountedForUpdate()
// is called right before LittleFS.end() and is never cleared: a reboot always follows the
// filesystem write (successful or not), so there's no "remount" event to reset it for.
inline std::atomic<bool> g_fsUnmountedForUpdate{false};

// True unless the running boot has unmounted LittleFS for an OTA filesystem write - callers
// that would otherwise touch LittleFS (comms.cpp's static-file/log routes) must check this
// first and answer without touching the filesystem when it's false.
inline bool mayServeFromFs() { return !g_fsUnmountedForUpdate.load(std::memory_order_acquire); }

// Called once, immediately before LittleFS.end(), by the OTA filesystem-write path.
inline void markFsUnmountedForUpdate()
{
    g_fsUnmountedForUpdate.store(true, std::memory_order_release);
}
