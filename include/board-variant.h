#pragma once

// Compile-time board identity, derived from the -DESP32_* flag each hardware
// env sets in platformio.ini. BOARD_VARIANT matches the PlatformIO env name so
// it lines up with the OTA release asset names (firmware-<variant>-<tag>.bin)
// and the per-board rows in a release manifest (#63).

#if defined(ESP32_WROOM)
#define BOARD_VARIANT "esp32_wroom"
#elif defined(ESP32_S3)
#define BOARD_VARIANT "esp32_s3_zero"
#elif defined(ESP32_C3)
#define BOARD_VARIANT "esp32_c3_zero"
#elif defined(NATIVE_BUILD)
#define BOARD_VARIANT "native"
#else
#error "No board variant defined - expected one of -DESP32_WROOM / -DESP32_S3 / -DESP32_C3"
#endif

// Bumped by hand only when partitions/andromeda_4mb.csv intentionally changes.
// Shipping OTA freezes that table for deployed units (it lives at 0x8000,
// outside every OTA-writable partition), so a change here is a deliberate
// "every unit needs a USB reflash" decision. test/test_partition_layout pins
// the CSV's offsets and sizes against this number so an accidental edit can't
// slip through CI.
#define PARTITION_LAYOUT_VERSION 1

// sha256 of the canonical CSV layout + the version above. build-scripts/
// check_partition_layout_lock.py (a pre: gate on every hardware build) fails
// the build unless this matches - so the CSV and PARTITION_LAYOUT_VERSION can
// never drift apart. Regenerate deliberately:
//   python build-scripts/check_partition_layout_lock.py --update
#define PARTITION_LAYOUT_DIGEST "a5042f8b20d70b630ca8d4308d8e07bce1e8fe7b886963fd0b68aa48ae0900d3"
