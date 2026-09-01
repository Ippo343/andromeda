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

}  // namespace OtaConfig
