#include <unity.h>

#include <cstring>
#include <string>

#include "device-identity.h"

// Clears whatever DeviceIdentity persisted so each test starts from "no
// custom name set", mirroring how test_geometry resets FactoryConfig via
// setModelId(UNKNOWN) between tests.
void setUp() { DeviceIdentity::setDeviceName(""); }
void tearDown() {}

void test_uid_is_four_alphanumeric_chars()
{
    const char* uid = DeviceIdentity::getUid();
    TEST_ASSERT_EQUAL(4, strlen(uid));
    for (int i = 0; i < 4; i++)
    {
        char c = uid[i];
        bool alnum = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z');
        TEST_ASSERT_TRUE(alnum);
    }
}

void test_uid_is_stable_across_calls()
{
    std::string first = DeviceIdentity::getUid();
    std::string second = DeviceIdentity::getUid();
    TEST_ASSERT_EQUAL_STRING(first.c_str(), second.c_str());
}

void test_default_name_is_not_customized()
{
    TEST_ASSERT_FALSE(DeviceIdentity::isNameCustomized());
}

void test_default_name_embeds_uid()
{
    std::string expected = std::string("Andromeda-") + DeviceIdentity::getUid();
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), DeviceIdentity::getDeviceName().c_str());
}

void test_set_device_name_persists_and_is_customized()
{
    DeviceIdentity::setDeviceName("Kitchen Lamp");
    TEST_ASSERT_TRUE(DeviceIdentity::isNameCustomized());
    TEST_ASSERT_EQUAL_STRING("Kitchen-Lamp", DeviceIdentity::getDeviceName().c_str());
}

void test_set_device_name_sanitizes_input()
{
    DeviceIdentity::setDeviceName("Living Room!!'");
    TEST_ASSERT_EQUAL_STRING("Living-Room", DeviceIdentity::getDeviceName().c_str());
}

void test_set_device_name_empty_clears_back_to_default()
{
    DeviceIdentity::setDeviceName("Kitchen");
    TEST_ASSERT_TRUE(DeviceIdentity::isNameCustomized());

    DeviceIdentity::setDeviceName("");
    TEST_ASSERT_FALSE(DeviceIdentity::isNameCustomized());

    std::string expected = std::string("Andromeda-") + DeviceIdentity::getUid();
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), DeviceIdentity::getDeviceName().c_str());
}

void test_mdns_hostname_is_lowercased_device_name()
{
    DeviceIdentity::setDeviceName("Kitchen Lamp");
    TEST_ASSERT_EQUAL_STRING("kitchen-lamp", DeviceIdentity::getMdnsHostname().c_str());
}

// setDeviceName() already truncates to DeviceUid::MAX_NAME_LENGTH (32), so a
// name this long fills getMdnsHostname()'s 32-byte copy buffer exactly,
// exercising its loop's buffer-full exit branch instead of the NUL-terminator
// exit every other (short-name) test hits.
void test_mdns_hostname_truncates_a_maximally_long_name()
{
    DeviceIdentity::setDeviceName("ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMN");  // 40 letters
    TEST_ASSERT_EQUAL_STRING("abcdefghijklmnopqrstuvwxyzabcdef",
                             DeviceIdentity::getMdnsHostname().c_str());
}

void test_mdns_hostname_matches_lowercased_default_name()
{
    std::string expected = std::string("andromeda-") + DeviceIdentity::getUid();
    for (auto& c : expected)
        if (c >= 'A' && c <= 'Z') c += 32;
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), DeviceIdentity::getMdnsHostname().c_str());
}

int main(int argc, char** argv)
{
    UNITY_BEGIN();

    RUN_TEST(test_uid_is_four_alphanumeric_chars);
    RUN_TEST(test_uid_is_stable_across_calls);
    RUN_TEST(test_default_name_is_not_customized);
    RUN_TEST(test_default_name_embeds_uid);
    RUN_TEST(test_set_device_name_persists_and_is_customized);
    RUN_TEST(test_set_device_name_sanitizes_input);
    RUN_TEST(test_set_device_name_empty_clears_back_to_default);
    RUN_TEST(test_mdns_hostname_is_lowercased_device_name);
    RUN_TEST(test_mdns_hostname_truncates_a_maximally_long_name);
    RUN_TEST(test_mdns_hostname_matches_lowercased_default_name);

    return UNITY_END();
}
