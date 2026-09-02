#pragma once

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
