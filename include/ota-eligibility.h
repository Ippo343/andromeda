#pragma once

#include <cstdint>

// Pure OTA decision helpers (#63), split out of src/ota-updater.cpp so the
// native suite (test/test_ota_eligibility) covers them. No Arduino, no
// networking - just the version-code arithmetic that decides whether an
// update may be applied and whether the last one actually took.

namespace OtaEligibility
{

// May the release identified by `latestCode` be flashed onto a device running
// `runningCode`?
//
//   - A strictly newer build is always eligible (the normal case).
//   - An *equal* build is eligible only when `fsDamaged` is set: the firmware
//     half is already correct, but an interrupted filesystem write (or a
//     failed LittleFS mount from any cause) left the device with no web UI and
//     the plain `latestCode > runningCode` check would refuse the very update
//     that repairs it. Re-flashing the current version rewrites both slots;
//     rewriting identical firmware is harmless and Update still verifies it.
//   - An older build is never eligible - a damaged filesystem must not become
//     a downgrade path.
inline bool shouldApply(uint32_t latestCode, uint32_t runningCode, bool fsDamaged)
{
    if (latestCode > runningCode) return true;
    if (fsDamaged && latestCode == runningCode) return true;
    return false;
}

// Did the last applied update fail to take? `lastAppliedCode` is what
// OtaConfig persisted just before the post-flash reboot; `runningCode` is the
// FIRMWARE_VERSION_CODE now executing. If we persisted a newer code than the
// one that came up, the device rebooted back into the old slot - the update
// silently did not apply. A never-OTA'd unit (lastAppliedCode == 0) reports
// false, so a factory / USB-flashed device never sees a phantom failure.
inline bool didNotTake(uint32_t lastAppliedCode, uint32_t runningCode)
{
    return lastAppliedCode != 0 && lastAppliedCode > runningCode;
}

}  // namespace OtaEligibility
