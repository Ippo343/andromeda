#include <unity.h>

#include <cmath>
#include <cstring>

#include "ws-metrics-builder.h"

using WsMetricsBuilder::MetricsSnapshot;

void test_ws_metrics_builder_setUp() {}
void test_ws_metrics_builder_tearDown() {}

namespace
{
MetricsSnapshot makeVolatileOnlySnapshot()
{
    MetricsSnapshot s{};
    s.uptimeMs = 812345;
    s.heapFree = 190112;
    s.heapMin = 150004;
    s.heapTotal = 327680;
    s.tempC = 42.5f;
    s.fps = 118.2f;
    s.rssi = -54;
    s.currentMa = 234;
    s.includeStatic = false;
    s.includeOta = false;
    return s;
}

MetricsSnapshot makeFullSnapshot()
{
    MetricsSnapshot s = makeVolatileOnlySnapshot();
    s.includeStatic = true;
    s.chip = "ESP32-S3";
    s.cpuMhz = 240;
    s.resetReason = 1;
    s.version = "v0.9-quiet-orbit";
    s.maxMilliamps = 9000;
    s.brightnessCeiling = 152;
    s.includeOta = true;
    s.updateAvailable = false;
    s.latestTag[0] = '\0';
    s.otaChannel = "stable";
    return s;
}
}  // namespace

void test_volatile_frame_type_and_fields()
{
    MetricsSnapshot s = makeVolatileOnlySnapshot();
    char buf[WsMetricsBuilder::JSON_CAPACITY];
    size_t len = WsMetricsBuilder::buildMetricsJson(s, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, len);

    StaticJsonDocument<WsMetricsBuilder::JSON_CAPACITY> doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, buf, len));
    TEST_ASSERT_EQUAL_STRING("metrics", doc["type"]);
    TEST_ASSERT_EQUAL_UINT32(812345, doc["uptimeMs"].as<uint32_t>());
    TEST_ASSERT_EQUAL_UINT32(190112, doc["heapFree"].as<uint32_t>());
    TEST_ASSERT_EQUAL_UINT32(150004, doc["heapMin"].as<uint32_t>());
    TEST_ASSERT_EQUAL_UINT32(327680, doc["heapTotal"].as<uint32_t>());
    TEST_ASSERT_FLOAT_WITHIN(0.01, 42.5, doc["tempC"].as<float>());
    TEST_ASSERT_FLOAT_WITHIN(0.01, 118.2, doc["fps"].as<float>());
    TEST_ASSERT_EQUAL_INT(-54, doc["rssi"].as<int>());
    TEST_ASSERT_EQUAL_UINT32(234, doc["currentMa"].as<uint32_t>());
}

void test_volatile_frame_omits_static_and_ota_keys()
{
    MetricsSnapshot s = makeVolatileOnlySnapshot();
    char buf[WsMetricsBuilder::JSON_CAPACITY];
    size_t len = WsMetricsBuilder::buildMetricsJson(s, buf, sizeof(buf));

    StaticJsonDocument<WsMetricsBuilder::JSON_CAPACITY> doc;
    deserializeJson(doc, buf, len);
    TEST_ASSERT_FALSE(doc.containsKey("chip"));
    TEST_ASSERT_FALSE(doc.containsKey("cpuMhz"));
    TEST_ASSERT_FALSE(doc.containsKey("resetReason"));
    TEST_ASSERT_FALSE(doc.containsKey("version"));
    TEST_ASSERT_FALSE(doc.containsKey("maxMilliamps"));
    TEST_ASSERT_FALSE(doc.containsKey("brightnessCeiling"));
    TEST_ASSERT_FALSE(doc.containsKey("updateAvailable"));
    TEST_ASSERT_FALSE(doc.containsKey("latestTag"));
    TEST_ASSERT_FALSE(doc.containsKey("otaChannel"));
}

void test_full_frame_has_all_fields()
{
    MetricsSnapshot s = makeFullSnapshot();
    char buf[WsMetricsBuilder::JSON_CAPACITY];
    size_t len = WsMetricsBuilder::buildMetricsJson(s, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, len);

    StaticJsonDocument<WsMetricsBuilder::JSON_CAPACITY> doc;
    deserializeJson(doc, buf, len);
    const char* keys[] = {
        "type",
        "uptimeMs",
        "heapFree",
        "heapMin",
        "heapTotal",
        "tempC",
        "fps",
        "rssi",
        "currentMa",
        "chip",
        "cpuMhz",
        "resetReason",
        "version",
        "maxMilliamps",
        "brightnessCeiling",
        "updateAvailable",
        "latestTag",
        "otaChannel",
    };
    for (const char* key : keys) { TEST_ASSERT_TRUE_MESSAGE(doc.containsKey(key), key); }
    TEST_ASSERT_EQUAL_STRING("ESP32-S3", doc["chip"]);
    TEST_ASSERT_EQUAL_STRING("v0.9-quiet-orbit", doc["version"]);
    TEST_ASSERT_EQUAL_STRING("stable", doc["otaChannel"]);
    TEST_ASSERT_EQUAL_UINT8(152, doc["brightnessCeiling"].as<uint8_t>());
}

void test_nan_temp_and_fps_serialize_as_null()
{
    MetricsSnapshot s = makeVolatileOnlySnapshot();
    s.tempC = NAN;
    s.fps = NAN;
    char buf[WsMetricsBuilder::JSON_CAPACITY];
    size_t len = WsMetricsBuilder::buildMetricsJson(s, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, len);

    StaticJsonDocument<WsMetricsBuilder::JSON_CAPACITY> doc;
    deserializeJson(doc, buf, len);
    TEST_ASSERT_TRUE(doc["tempC"].isNull());
    TEST_ASSERT_TRUE(doc["fps"].isNull());
}

void test_ordinary_values_round_trip()
{
    MetricsSnapshot s = makeFullSnapshot();
    s.updateAvailable = true;
    strncpy(s.latestTag, "v1.0-bright-nebula", sizeof(s.latestTag) - 1);
    s.latestTag[sizeof(s.latestTag) - 1] = '\0';
    char buf[WsMetricsBuilder::JSON_CAPACITY];
    size_t len = WsMetricsBuilder::buildMetricsJson(s, buf, sizeof(buf));

    StaticJsonDocument<WsMetricsBuilder::JSON_CAPACITY> doc;
    deserializeJson(doc, buf, len);
    TEST_ASSERT_TRUE(doc["updateAvailable"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("v1.0-bright-nebula", doc["latestTag"]);
}

// ArduinoJson's serializeJson(doc, char*, size_t) overload never writes past
// `outBufferSize` - it truncates and returns the number of bytes actually
// written (bounded by outBufferSize), not snprintf's "would-be length"
// convention, and it does not NUL-terminate. The 0-on-failure case in
// buildMetricsJson's own doc comment is about the *document* overflowing its
// fixed JSON_CAPACITY pool (not reachable through real snapshot data here,
// since every string field is a const char* ArduinoJson stores by reference
// rather than copies into the pool - see JSON_CAPACITY's own comment for the
// measured headroom), not an undersized destination buffer - this test pins
// that a small destination buffer degrades safely (no overrun) instead.
void test_undersized_output_buffer_truncates_safely()
{
    MetricsSnapshot s = makeFullSnapshot();
    char tiny[8];
    size_t len = WsMetricsBuilder::buildMetricsJson(s, tiny, sizeof(tiny));
    TEST_ASSERT_LESS_OR_EQUAL(sizeof(tiny), len);
    TEST_ASSERT_GREATER_THAN(0, len);
}

void run_test_ws_metrics_builder_tests()
{
    RUN_TEST(test_volatile_frame_type_and_fields);
    RUN_TEST(test_volatile_frame_omits_static_and_ota_keys);
    RUN_TEST(test_full_frame_has_all_fields);
    RUN_TEST(test_nan_temp_and_fps_serialize_as_null);
    RUN_TEST(test_ordinary_values_round_trip);
    RUN_TEST(test_undersized_output_buffer_truncates_safely);
}
