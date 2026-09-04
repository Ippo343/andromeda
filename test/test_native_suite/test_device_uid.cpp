#include <unity.h>

#include <cstring>

#include "device-uid.h"

void test_device_uid_setUp() {}
void test_device_uid_tearDown() {}

void test_generate_is_deterministic()
{
    uint8_t mac[6] = {0x24, 0x0A, 0xC4, 0x00, 0x01, 0x02};
    char a[DeviceUid::LENGTH + 1];
    char b[DeviceUid::LENGTH + 1];
    DeviceUid::generate(mac, a);
    DeviceUid::generate(mac, b);
    TEST_ASSERT_EQUAL_STRING(a, b);
}

void test_generate_produces_length_alphanumeric_chars()
{
    uint8_t mac[6] = {0x24, 0x0A, 0xC4, 0x00, 0x01, 0x02};
    char uid[DeviceUid::LENGTH + 1];
    DeviceUid::generate(mac, uid);

    TEST_ASSERT_EQUAL(DeviceUid::LENGTH, strlen(uid));
    for (size_t i = 0; i < DeviceUid::LENGTH; i++)
    {
        char c = uid[i];
        bool alnum = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z');
        TEST_ASSERT_TRUE(alnum);
    }
}

void test_generate_differs_across_macs()
{
    uint8_t macA[6] = {0x24, 0x0A, 0xC4, 0x00, 0x01, 0x02};
    uint8_t macB[6] = {0x24, 0x0A, 0xC4, 0x00, 0x01, 0x03};
    char uidA[DeviceUid::LENGTH + 1];
    char uidB[DeviceUid::LENGTH + 1];
    DeviceUid::generate(macA, uidA);
    DeviceUid::generate(macB, uidB);

    TEST_ASSERT_NOT_EQUAL(0, strcmp(uidA, uidB));
}

void test_to_lower_ascii_only_affects_letters()
{
    char s[] = "Andromeda-A1B2!";
    DeviceUid::toLowerAscii(s);
    TEST_ASSERT_EQUAL_STRING("andromeda-a1b2!", s);
}

void test_sanitize_keeps_alphanumerics()
{
    char out[32];
    size_t len = DeviceUid::sanitize("Kitchen42", out, sizeof(out));
    TEST_ASSERT_EQUAL(9, len);
    TEST_ASSERT_EQUAL_STRING("Kitchen42", out);
}

void test_sanitize_maps_whitespace_and_underscore_to_hyphen()
{
    char out[32];
    DeviceUid::sanitize("Living Room_Lamp", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Living-Room-Lamp", out);
}

void test_sanitize_drops_disallowed_characters()
{
    char out[32];
    DeviceUid::sanitize("Kit\"chen'!@#$", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Kitchen", out);
}

void test_sanitize_collapses_repeated_hyphens()
{
    char out[32];
    DeviceUid::sanitize("a---b   c", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("a-b-c", out);
}

void test_sanitize_trims_leading_and_trailing_hyphens()
{
    char out[32];
    DeviceUid::sanitize("--Kitchen--", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Kitchen", out);
}

void test_sanitize_truncates_to_max_name_length()
{
    char input[64];
    for (size_t i = 0; i < sizeof(input) - 1; i++) input[i] = 'a';
    input[sizeof(input) - 1] = '\0';

    char out[64];
    size_t len = DeviceUid::sanitize(input, out, sizeof(out));
    TEST_ASSERT_EQUAL(DeviceUid::MAX_NAME_LENGTH, len);
}

void test_sanitize_truncates_to_output_buffer_size()
{
    char out[5];
    size_t len = DeviceUid::sanitize("LongerName", out, sizeof(out));
    TEST_ASSERT_EQUAL(4, len);
    TEST_ASSERT_EQUAL_STRING("Long", out);
}

void test_sanitize_empty_input_yields_empty_output()
{
    char out[32];
    size_t len = DeviceUid::sanitize("", out, sizeof(out));
    TEST_ASSERT_EQUAL(0, len);
    TEST_ASSERT_EQUAL_STRING("", out);
}

void test_sanitize_all_invalid_input_yields_empty_output()
{
    char out[32];
    size_t len = DeviceUid::sanitize("!!! ___ ---", out, sizeof(out));
    TEST_ASSERT_EQUAL(0, len);
}

void run_test_device_uid_tests()
{
    RUN_TEST(test_generate_is_deterministic);
    RUN_TEST(test_generate_produces_length_alphanumeric_chars);
    RUN_TEST(test_generate_differs_across_macs);
    RUN_TEST(test_to_lower_ascii_only_affects_letters);
    RUN_TEST(test_sanitize_keeps_alphanumerics);
    RUN_TEST(test_sanitize_maps_whitespace_and_underscore_to_hyphen);
    RUN_TEST(test_sanitize_drops_disallowed_characters);
    RUN_TEST(test_sanitize_collapses_repeated_hyphens);
    RUN_TEST(test_sanitize_trims_leading_and_trailing_hyphens);
    RUN_TEST(test_sanitize_truncates_to_max_name_length);
    RUN_TEST(test_sanitize_truncates_to_output_buffer_size);
    RUN_TEST(test_sanitize_empty_input_yields_empty_output);
    RUN_TEST(test_sanitize_all_invalid_input_yields_empty_output);
}
