#pragma once

#include <cstddef>
#include <cstdint>

// Persisted OTA state (#63), in the shared "device" NVS namespace. Kept in
// its own translation unit - not folded into src/ota-updater.cpp - so it
// compiles for the native suite (against test/mocks/Preferences.h) the same
// way BrightnessConfig / StartupStateConfig do, while ota-updater.cpp stays
// out of the native build with the rest of the networking code.

namespace OtaConfig
{

// Opt-in dev/beta channel. Default (never set, or a corrupt value): false -
// a unit only ever tracks pre-releases if someone deliberately ticked the box.
void persistDevChannel(bool dev);
bool devChannel();

// Recorded after a successful update so the next check can tell "already on
// this build" from "never updated", and skip re-flashing an unchanged
// filesystem. fsMd5 is the 32-char hex from the applied manifest row.
void persistApplied(uint32_t versionCode, const char* fsMd5);
uint32_t lastAppliedCode();

// Writes the stored applied-filesystem MD5 into out[outCap]. Returns false if
// none is stored or it doesn't fit (caller then treats the FS as "unknown"
// and re-flashes it).
bool appliedFsMd5(char* out, size_t outCap);

// Partial-failure flag. When an OTA writes the firmware slot but then fails
// the filesystem half, the device still reboots (the fw slot is bootable) -
// which throws away the Failed state, so /ota-status shows "rebooting" and
// the owner never learns the FS didn't take, possibly with no web UI left to
// find out from. persistPartialFailure() records it durably so the *next*
// boot's /ota-status can surface it; a later clean apply (persistApplied with
// a real fs md5) clears it, as does clearPartialFailure().
void persistPartialFailure(const char* reason);
void clearPartialFailure();
bool partialFailurePending();

// Writes the stored partial-failure reason into out[outCap] (always
// NUL-terminated). Returns false if no partial failure is pending or the
// reason doesn't fit.
bool partialFailureReason(char* out, size_t outCap);

}  // namespace OtaConfig
