#include <unity.h>

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
    TEST_ASSERT_TRUE(
        WsCommandParser::parseBrightness("{\"type\":\"brightness\",\"value\":128}", value));
    TEST_ASSERT_EQUAL(128, value);
}

void test_rejects_brightness_out_of_range()
{
    uint8_t value;
    TEST_ASSERT_FALSE(
        WsCommandParser::parseBrightness("{\"type\":\"brightness\",\"value\":-1}", value));
    TEST_ASSERT_FALSE(
        WsCommandParser::parseBrightness("{\"type\":\"brightness\",\"value\":256}", value));
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

int main(int argc, char** argv)
{
    UNITY_BEGIN();

    RUN_TEST(test_parses_next);
    RUN_TEST(test_parses_hold);
    RUN_TEST(test_parses_power_off);
    RUN_TEST(test_parses_power_on);
    RUN_TEST(test_parses_reboot);
    RUN_TEST(test_parses_color);
    RUN_TEST(test_rejects_color_with_missing_field);
    RUN_TEST(test_rejects_color_with_out_of_range_field);
    RUN_TEST(test_parses_brightness);
    RUN_TEST(test_rejects_brightness_out_of_range);
    RUN_TEST(test_generic_parse_rejects_brightness);
    RUN_TEST(test_parses_model);
    RUN_TEST(test_rejects_model_with_negative_id);
    RUN_TEST(test_rejects_unrecognized_type);
    RUN_TEST(test_rejects_malformed_message);

    return UNITY_END();
}
