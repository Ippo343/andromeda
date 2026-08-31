#include <unity.h>

#include <string>

#include "ws-command-parser.h"

void setUp() {}
void tearDown() {}

void test_parses_next()
{
    Command out;
    TEST_ASSERT_TRUE(WsCommandParser::parse("{\"type\":\"next\"}", out));
    TEST_ASSERT_EQUAL(static_cast<int>(CommandType::NEXT), static_cast<int>(out.type));
}

void test_parses_hold()
{
    Command out;
    TEST_ASSERT_TRUE(WsCommandParser::parse("{\"type\":\"hold\"}", out));
    TEST_ASSERT_EQUAL(static_cast<int>(CommandType::HOLD), static_cast<int>(out.type));
}

void test_parses_resume()
{
    Command out;
    TEST_ASSERT_TRUE(WsCommandParser::parse("{\"type\":\"resume\"}", out));
    TEST_ASSERT_EQUAL(static_cast<int>(CommandType::RESUME), static_cast<int>(out.type));
}

void test_parses_power_off()
{
    Command out;
    TEST_ASSERT_TRUE(WsCommandParser::parse("{\"type\":\"power_off\"}", out));
    TEST_ASSERT_EQUAL(static_cast<int>(CommandType::POWER_OFF), static_cast<int>(out.type));
}

void test_parses_power_on()
{
    Command out;
    TEST_ASSERT_TRUE(WsCommandParser::parse("{\"type\":\"power_on\"}", out));
    TEST_ASSERT_EQUAL(static_cast<int>(CommandType::POWER_ON), static_cast<int>(out.type));
}

void test_parses_reboot()
{
    Command out;
    TEST_ASSERT_TRUE(WsCommandParser::parse("{\"type\":\"reboot\"}", out));
    TEST_ASSERT_EQUAL(static_cast<int>(CommandType::REBOOT), static_cast<int>(out.type));
}

void test_parses_color()
{
    Command out;
    TEST_ASSERT_TRUE(
        WsCommandParser::parse("{\"type\":\"color\",\"r\":10,\"g\":20,\"b\":30}", out));
    TEST_ASSERT_EQUAL(static_cast<int>(CommandType::COLOR), static_cast<int>(out.type));
    TEST_ASSERT_EQUAL(10, out.r);
    TEST_ASSERT_EQUAL(20, out.g);
    TEST_ASSERT_EQUAL(30, out.b);
}

void test_rejects_color_with_missing_field()
{
    Command out;
    TEST_ASSERT_FALSE(WsCommandParser::parse("{\"type\":\"color\",\"r\":10,\"g\":20}", out));
}

void test_rejects_color_with_out_of_range_field()
{
    Command out;
    TEST_ASSERT_FALSE(
        WsCommandParser::parse("{\"type\":\"color\",\"r\":300,\"g\":0,\"b\":0}", out));
}

void test_parses_brightness()
{
    uint8_t value;
    bool commit;
    TEST_ASSERT_TRUE(
        WsCommandParser::parseBrightness("{\"type\":\"brightness\",\"value\":128}", value, commit));
    TEST_ASSERT_EQUAL(128, value);
    TEST_ASSERT_FALSE(commit);
}

void test_parses_brightness_commit_flag()
{
    uint8_t value;
    bool commit;
    TEST_ASSERT_TRUE(WsCommandParser::parseBrightness(
        "{\"type\":\"brightness\",\"value\":200,\"commit\":true}", value, commit));
    TEST_ASSERT_EQUAL(200, value);
    TEST_ASSERT_TRUE(commit);
}

void test_rejects_brightness_out_of_range()
{
    uint8_t value;
    bool commit;
    TEST_ASSERT_FALSE(
        WsCommandParser::parseBrightness("{\"type\":\"brightness\",\"value\":-1}", value, commit));
    TEST_ASSERT_FALSE(
        WsCommandParser::parseBrightness("{\"type\":\"brightness\",\"value\":256}", value, commit));
}

void test_generic_parse_rejects_brightness()
{
    // BRIGHTNESS is a fast-path type handled entirely by parseBrightness()
    // now - the generic parser doesn't know it and must treat it as
    // unrecognized (see the comms.cpp dispatch: parseBrightness() is tried
    // first, parse() only sees what falls through).
    Command out;
    TEST_ASSERT_FALSE(WsCommandParser::parse("{\"type\":\"brightness\",\"value\":128}", out));
}

void test_parses_model()
{
    Command out;
    TEST_ASSERT_TRUE(WsCommandParser::parse("{\"type\":\"model\",\"id\":3}", out));
    TEST_ASSERT_EQUAL(static_cast<int>(CommandType::MODEL), static_cast<int>(out.type));
    TEST_ASSERT_EQUAL(3, out.modelId);
}

void test_rejects_model_with_negative_id()
{
    Command out;
    TEST_ASSERT_FALSE(WsCommandParser::parse("{\"type\":\"model\",\"id\":-1}", out));
}

void test_parses_effect()
{
    Command out;
    TEST_ASSERT_TRUE(WsCommandParser::parse("{\"type\":\"effect\",\"id\":5}", out));
    TEST_ASSERT_EQUAL(static_cast<int>(CommandType::EFFECT), static_cast<int>(out.type));
    TEST_ASSERT_EQUAL(5, out.effectId);
}

void test_rejects_effect_with_missing_id()
{
    Command out;
    TEST_ASSERT_FALSE(WsCommandParser::parse("{\"type\":\"effect\"}", out));
}

void test_rejects_effect_with_negative_id()
{
    Command out;
    TEST_ASSERT_FALSE(WsCommandParser::parse("{\"type\":\"effect\",\"id\":-1}", out));
}

void test_rejects_effect_with_out_of_range_id()
{
    Command out;
    TEST_ASSERT_FALSE(WsCommandParser::parse("{\"type\":\"effect\",\"id\":256}", out));
}

void test_parses_device_name()
{
    Command out;
    TEST_ASSERT_TRUE(
        WsCommandParser::parse("{\"type\":\"device_name\",\"name\":\"Kitchen\"}", out));
    TEST_ASSERT_EQUAL(static_cast<int>(CommandType::DEVICE_NAME), static_cast<int>(out.type));
    TEST_ASSERT_EQUAL_STRING("Kitchen", out.deviceName);
}

void test_parses_device_name_truncates_to_buffer_size()
{
    Command out;
    std::string longName(DeviceUid::MAX_NAME_LENGTH + 20, 'x');
    std::string json = "{\"type\":\"device_name\",\"name\":\"" + longName + "\"}";
    TEST_ASSERT_TRUE(WsCommandParser::parse(json.c_str(), out));
    TEST_ASSERT_EQUAL(DeviceUid::MAX_NAME_LENGTH, strlen(out.deviceName));
}

void test_rejects_device_name_with_missing_field()
{
    Command out;
    TEST_ASSERT_FALSE(WsCommandParser::parse("{\"type\":\"device_name\"}", out));
}

void test_rejects_device_name_unterminated_string()
{
    Command out;
    TEST_ASSERT_FALSE(WsCommandParser::parse("{\"type\":\"device_name\",\"name\":\"Kitchen}", out));
}

void test_rejects_unrecognized_type()
{
    Command out;
    TEST_ASSERT_FALSE(WsCommandParser::parse("{\"type\":\"unknown\"}", out));
}

void test_rejects_malformed_message()
{
    Command out;
    TEST_ASSERT_FALSE(WsCommandParser::parse("not json at all", out));
}

void test_parse_null_json_is_rejected_not_ub()
{
    Command out;
    TEST_ASSERT_FALSE(WsCommandParser::parse(nullptr, out));
}

void test_parse_brightness_null_json_is_rejected_not_ub()
{
    uint8_t value;
    bool commit;
    TEST_ASSERT_FALSE(WsCommandParser::parseBrightness(nullptr, value, commit));
}

void test_rejects_color_with_string_value()
{
    // strtol("abc", ...) parses as 0 - previously indistinguishable from an explicit 0,
    // silently turning e.g. a garbage red channel into black instead of being rejected.
    Command out;
    TEST_ASSERT_FALSE(
        WsCommandParser::parse("{\"type\":\"color\",\"r\":\"abc\",\"g\":0,\"b\":0}", out));
}

void test_rejects_color_with_null_value()
{
    Command out;
    TEST_ASSERT_FALSE(
        WsCommandParser::parse("{\"type\":\"color\",\"r\":null,\"g\":0,\"b\":0}", out));
}

void test_rejects_color_with_boolean_value()
{
    Command out;
    TEST_ASSERT_FALSE(
        WsCommandParser::parse("{\"type\":\"color\",\"r\":true,\"g\":0,\"b\":0}", out));
}

void test_rejects_color_with_empty_value()
{
    Command out;
    TEST_ASSERT_FALSE(WsCommandParser::parse("{\"type\":\"color\",\"r\":,\"g\":0,\"b\":0}", out));
}

void test_rejects_brightness_with_string_value()
{
    uint8_t value;
    bool commit;
    TEST_ASSERT_FALSE(WsCommandParser::parseBrightness(
        "{\"type\":\"brightness\",\"value\":\"abc\"}", value, commit));
}

void test_rejects_brightness_with_null_value()
{
    uint8_t value;
    bool commit;
    TEST_ASSERT_FALSE(WsCommandParser::parseBrightness("{\"type\":\"brightness\",\"value\":null}",
                                                       value, commit));
}

void test_parse_tolerates_whitespace_after_colon_for_numeric_fields()
{
    // findField() (used for r/g/b/id/value) now skips whitespace after the key - a strict,
    // no-whitespace match previously rejected perfectly valid JSON with a space after the
    // colon, which any real JSON serializer (the browser's own JSON.stringify included) is
    // free to emit. Note this is specifically about findField()'s numeric fields, not the
    // fixed `"type":"next"`-style literal command matches, which are unaffected either way
    // since this client's own JSON.stringify never emits that whitespace in practice.
    Command out;
    TEST_ASSERT_TRUE(
        WsCommandParser::parse("{\"type\":\"color\",\"r\": 10,\"g\":\t20,\"b\":30}", out));
    TEST_ASSERT_EQUAL(10, out.r);
    TEST_ASSERT_EQUAL(20, out.g);
}

void test_parses_negative_color_value_after_whitespace()
{
    // Whitespace-skipping must not interfere with a legitimate leading '-' (rejected later by
    // the existing range check, not by findField() itself).
    Command out;
    TEST_ASSERT_FALSE(
        WsCommandParser::parse("{\"type\":\"color\",\"r\": -1,\"g\":0,\"b\":0}", out));
}

void test_rejects_model_with_unregistered_id()
{
    // Handler-side validation (mission-control.cpp) is what actually protects the device from
    // a null-config brick - the parser itself only range-checks (id >= 0), matching
    // test_rejects_model_with_negative_id above. This is a regression guard for that
    // boundary staying exactly where it is: parse() must keep accepting any non-negative id
    // so the reject-unknown-id logic has a value to reject in the first place.
    Command out;
    TEST_ASSERT_TRUE(WsCommandParser::parse("{\"type\":\"model\",\"id\":65000}", out));
    TEST_ASSERT_EQUAL(65000, out.modelId);
}

int main(int argc, char** argv)
{
    UNITY_BEGIN();

    RUN_TEST(test_parses_next);
    RUN_TEST(test_parses_hold);
    RUN_TEST(test_parses_resume);
    RUN_TEST(test_parses_power_off);
    RUN_TEST(test_parses_power_on);
    RUN_TEST(test_parses_reboot);
    RUN_TEST(test_parses_color);
    RUN_TEST(test_rejects_color_with_missing_field);
    RUN_TEST(test_rejects_color_with_out_of_range_field);
    RUN_TEST(test_parses_brightness);
    RUN_TEST(test_parses_brightness_commit_flag);
    RUN_TEST(test_rejects_brightness_out_of_range);
    RUN_TEST(test_generic_parse_rejects_brightness);
    RUN_TEST(test_parses_model);
    RUN_TEST(test_rejects_model_with_negative_id);
    RUN_TEST(test_parses_effect);
    RUN_TEST(test_rejects_effect_with_missing_id);
    RUN_TEST(test_rejects_effect_with_negative_id);
    RUN_TEST(test_rejects_effect_with_out_of_range_id);
    RUN_TEST(test_parses_device_name);
    RUN_TEST(test_parses_device_name_truncates_to_buffer_size);
    RUN_TEST(test_rejects_device_name_with_missing_field);
    RUN_TEST(test_rejects_device_name_unterminated_string);
    RUN_TEST(test_rejects_unrecognized_type);
    RUN_TEST(test_rejects_malformed_message);
    RUN_TEST(test_parse_null_json_is_rejected_not_ub);
    RUN_TEST(test_parse_brightness_null_json_is_rejected_not_ub);
    RUN_TEST(test_rejects_color_with_string_value);
    RUN_TEST(test_rejects_color_with_null_value);
    RUN_TEST(test_rejects_color_with_boolean_value);
    RUN_TEST(test_rejects_color_with_empty_value);
    RUN_TEST(test_rejects_brightness_with_string_value);
    RUN_TEST(test_rejects_brightness_with_null_value);
    RUN_TEST(test_parse_tolerates_whitespace_after_colon_for_numeric_fields);
    RUN_TEST(test_parses_negative_color_value_after_whitespace);
    RUN_TEST(test_rejects_model_with_unregistered_id);

    return UNITY_END();
}
