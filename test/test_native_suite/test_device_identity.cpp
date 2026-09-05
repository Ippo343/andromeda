#include <unity.h>

#include <cstring>
#include <string>

#include "device-identity.h"
#include "geometry/geometry.h"
#include "geometry/model_registry.h"

// Clears whatever DeviceIdentity persisted so each test starts from "no
// custom name set", mirroring how test_geometry resets the Preferences mock
// (Preferences::resetAllForTests()) between its FactoryConfig tests. Also
// pins GEOMETRY to a known model (ANDROMEDA_MK0, matching every pre-existing
// "Andromeda-" expectation below) since getDefaultName() now reads the
// running model, and GEOMETRY is a global whichever suite last ran (e.g.
// test_animations, which runs immediately before this one - see runner.cpp)
// left it as - without this, these tests would be order-dependent.
void test_device_identity_setUp()
{
    DeviceIdentity::setDeviceName("");
    GEOMETRY.initializeForTest(ModelId::ANDROMEDA_MK0);
}
void test_device_identity_tearDown() {}

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

// --- Bare (UID-free) model mDNS label (#210) --------------------------------

void test_model_mdns_hostname_is_bare_lowercased_prefix()
{
    GEOMETRY.initializeForTest(ModelId::L70_MK1);
    TEST_ASSERT_EQUAL_STRING("l70", DeviceIdentity::getModelMdnsHostname().c_str());
}

void test_model_mdns_hostname_falls_back_to_andromeda_for_mk0()
{
    GEOMETRY.initializeForTest(ModelId::ANDROMEDA_MK0);
    TEST_ASSERT_EQUAL_STRING("andromeda", DeviceIdentity::getModelMdnsHostname().c_str());
}

// --- Model-derived default name (#105/#187) ---------------------------------

void test_default_name_derives_from_l70_mk1()
{
    GEOMETRY.initializeForTest(ModelId::L70_MK1);
    std::string expected = std::string("L70-") + DeviceIdentity::getUid();
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), DeviceIdentity::getDeviceName().c_str());
}

// L10_MK0 and L10_MK1 are two distinct, frozen model numbers (see
// model_config.h's #190 renumbering comment) that intentionally collapse to
// the same SSID prefix, since both display names start with "L10 ".
void test_default_name_derives_from_l10_mk0_and_mk1()
{
    GEOMETRY.initializeForTest(ModelId::L10_MK0);
    std::string expected = std::string("L10-") + DeviceIdentity::getUid();
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), DeviceIdentity::getDeviceName().c_str());

    GEOMETRY.initializeForTest(ModelId::L10_MK1);
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), DeviceIdentity::getDeviceName().c_str());
}

// A single-token model name (no space) is used whole, not truncated at a
// token boundary that doesn't exist.
void test_default_name_uses_a_single_token_name_whole()
{
    GEOMETRY.initializeForTest(ModelId::GRID_TEST_DEVICE);
    std::string expected = std::string("Grid-") + DeviceIdentity::getUid();
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), DeviceIdentity::getDeviceName().c_str());
}

// Table-driven over the whole registry: every registered model's derived
// prefix is non-empty, SSID-safe, and the resulting name still fits
// DeviceUid::MAX_NAME_LENGTH.
void test_every_registered_model_yields_a_valid_ssid_safe_name()
{
    for (size_t i = 0; i < NUM_MODELS; i++)
    {
        GEOMETRY.initializeForTest(MODEL_REGISTRY[i]->id);
        std::string name = DeviceIdentity::getDeviceName().c_str();
        TEST_ASSERT_TRUE_MESSAGE(name.length() > 0, MODEL_REGISTRY[i]->name);
        TEST_ASSERT_TRUE_MESSAGE(name.length() <= DeviceUid::MAX_NAME_LENGTH,
                                 MODEL_REGISTRY[i]->name);
        for (char c : name)
        {
            bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '-';
            TEST_ASSERT_TRUE_MESSAGE(safe, MODEL_REGISTRY[i]->name);
        }
    }
}

// A custom name still overrides the model-derived default...
void test_custom_name_overrides_model_derived_default()
{
    GEOMETRY.initializeForTest(ModelId::L70_MK1);
    DeviceIdentity::setDeviceName("Kitchen Lamp");
    TEST_ASSERT_EQUAL_STRING("Kitchen-Lamp", DeviceIdentity::getDeviceName().c_str());
}

// ...and clearing it falls back to the *derived* name, not unconditionally
// to "Andromeda-" - the pre-#105 behavior.
void test_clearing_custom_name_falls_back_to_derived_name_not_andromeda()
{
    GEOMETRY.initializeForTest(ModelId::L70_MK1);
    DeviceIdentity::setDeviceName("Kitchen Lamp");
    DeviceIdentity::setDeviceName("");

    std::string expected = std::string("L70-") + DeviceIdentity::getUid();
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), DeviceIdentity::getDeviceName().c_str());
}

void run_test_device_identity_tests()
{
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
    RUN_TEST(test_model_mdns_hostname_is_bare_lowercased_prefix);
    RUN_TEST(test_model_mdns_hostname_falls_back_to_andromeda_for_mk0);
    RUN_TEST(test_default_name_derives_from_l70_mk1);
    RUN_TEST(test_default_name_derives_from_l10_mk0_and_mk1);
    RUN_TEST(test_default_name_uses_a_single_token_name_whole);
    RUN_TEST(test_every_registered_model_yields_a_valid_ssid_safe_name);
    RUN_TEST(test_custom_name_overrides_model_derived_default);
    RUN_TEST(test_clearing_custom_name_falls_back_to_derived_name_not_andromeda);
}
