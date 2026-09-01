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
    return UNITY_END();
}
