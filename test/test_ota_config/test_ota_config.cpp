// Native unit tests for OtaConfig (src/ota-config.cpp) - the persisted OTA
// state (#63), round-tripped through the in-memory Preferences mock.
#include <Preferences.h>
#include <unity.h>

#include <cstring>

#include "ota-config.h"

namespace
{
void wipeDeviceNamespace()
{
    Preferences prefs;
    prefs.begin("device", false);
    prefs.clear();
    prefs.end();
}
}  // namespace

void setUp() { wipeDeviceNamespace(); }
void tearDown() { wipeDeviceNamespace(); }

void test_dev_channel_defaults_to_stable() { TEST_ASSERT_FALSE(OtaConfig::devChannel()); }

void test_dev_channel_round_trips()
{
    OtaConfig::persistDevChannel(true);
    TEST_ASSERT_TRUE(OtaConfig::devChannel());

    OtaConfig::persistDevChannel(false);
    TEST_ASSERT_FALSE(OtaConfig::devChannel());
}

void test_applied_version_defaults_to_zero()
{
    TEST_ASSERT_EQUAL_UINT32(0, OtaConfig::lastAppliedCode());
}

void test_applied_state_round_trips()
{
    OtaConfig::persistApplied(70000u, "0123456789abcdef0123456789abcdef");

    // 70000 exceeds uint16 - make sure it isn't being narrowed.
    TEST_ASSERT_EQUAL_UINT32(70000u, OtaConfig::lastAppliedCode());

    char md5[33] = {};
    TEST_ASSERT_TRUE(OtaConfig::appliedFsMd5(md5, sizeof(md5)));
    TEST_ASSERT_EQUAL_STRING("0123456789abcdef0123456789abcdef", md5);
}

void test_applied_fs_md5_reports_absent_when_never_set()
{
    char md5[33] = {};
    TEST_ASSERT_FALSE(OtaConfig::appliedFsMd5(md5, sizeof(md5)));
}

void test_applied_fs_md5_rejects_undersized_buffer()
{
    OtaConfig::persistApplied(1u, "0123456789abcdef0123456789abcdef");
    char tiny[8] = {};
    TEST_ASSERT_FALSE(OtaConfig::appliedFsMd5(tiny, sizeof(tiny)));
}

void test_persist_applied_tolerates_null_md5()
{
    OtaConfig::persistApplied(5u, nullptr);
    TEST_ASSERT_EQUAL_UINT32(5u, OtaConfig::lastAppliedCode());
    char md5[33] = {};
    TEST_ASSERT_FALSE(OtaConfig::appliedFsMd5(md5, sizeof(md5)));
}

// --- partial-failure flag (#194) -----------------------------------------

void test_partial_failure_absent_on_a_clean_device()
{
    TEST_ASSERT_FALSE(OtaConfig::partialFailurePending());
    char reason[96] = {};
    TEST_ASSERT_FALSE(OtaConfig::partialFailureReason(reason, sizeof(reason)));
}

// Good weather: a normal clean apply reads back with no partial-failure flag.
void test_clean_apply_leaves_no_partial_failure()
{
    OtaConfig::persistApplied(70000u, "0123456789abcdef0123456789abcdef");
    TEST_ASSERT_FALSE(OtaConfig::partialFailurePending());
}

// Bad weather: the FS half failed - persist it, then read it back through a
// fresh Preferences open (what a reboot looks like to this code) and confirm
// the flag and its reason survived.
void test_partial_failure_survives_a_reboot()
{
    OtaConfig::persistPartialFailure("short write flashing image");

    TEST_ASSERT_TRUE(OtaConfig::partialFailurePending());
    char reason[96] = {};
    TEST_ASSERT_TRUE(OtaConfig::partialFailureReason(reason, sizeof(reason)));
    TEST_ASSERT_EQUAL_STRING("short write flashing image", reason);
}

// The degraded apply the firmware-only recovery path does (empty md5) must
// NOT clear the flag - the FS is still broken.
void test_degraded_apply_keeps_partial_failure()
{
    OtaConfig::persistPartialFailure("bad md5 in manifest - refusing to flash unverified");
    OtaConfig::persistApplied(70001u, "");
    TEST_ASSERT_TRUE(OtaConfig::partialFailurePending());

    // ...but the next genuinely clean apply does clear it.
    OtaConfig::persistApplied(70002u, "fedcba9876543210fedcba9876543210");
    TEST_ASSERT_FALSE(OtaConfig::partialFailurePending());
    char reason[96] = {};
    TEST_ASSERT_FALSE(OtaConfig::partialFailureReason(reason, sizeof(reason)));
}

void test_clear_partial_failure()
{
    OtaConfig::persistPartialFailure("image download failed");
    OtaConfig::clearPartialFailure();
    TEST_ASSERT_FALSE(OtaConfig::partialFailurePending());
}

void test_partial_failure_reason_rejects_undersized_buffer()
{
    OtaConfig::persistPartialFailure("image size does not match manifest");
    char tiny[8] = {};
    TEST_ASSERT_FALSE(OtaConfig::partialFailureReason(tiny, sizeof(tiny)));
}

void test_partial_failure_tolerates_null_reason()
{
    OtaConfig::persistPartialFailure(nullptr);
    TEST_ASSERT_TRUE(OtaConfig::partialFailurePending());
    // Pending with an empty reason still round-trips: an empty NUL-terminated
    // string that fits the buffer, reported as present.
    char reason[96] = {"x"};
    TEST_ASSERT_TRUE(OtaConfig::partialFailureReason(reason, sizeof(reason)));
    TEST_ASSERT_EQUAL_STRING("", reason);
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_dev_channel_defaults_to_stable);
    RUN_TEST(test_dev_channel_round_trips);
    RUN_TEST(test_applied_version_defaults_to_zero);
    RUN_TEST(test_applied_state_round_trips);
    RUN_TEST(test_applied_fs_md5_reports_absent_when_never_set);
    RUN_TEST(test_applied_fs_md5_rejects_undersized_buffer);
    RUN_TEST(test_persist_applied_tolerates_null_md5);
    RUN_TEST(test_partial_failure_absent_on_a_clean_device);
    RUN_TEST(test_clean_apply_leaves_no_partial_failure);
    RUN_TEST(test_partial_failure_survives_a_reboot);
    RUN_TEST(test_degraded_apply_keeps_partial_failure);
    RUN_TEST(test_clear_partial_failure);
    RUN_TEST(test_partial_failure_reason_rejects_undersized_buffer);
    RUN_TEST(test_partial_failure_tolerates_null_reason);
    return UNITY_END();
}
