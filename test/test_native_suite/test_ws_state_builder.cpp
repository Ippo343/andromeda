#include <unity.h>

#include "geometry/model_registry.h"
#include "ws-state-builder.h"

using WsStateBuilder::DeviceState;
using WsStateBuilder::ModelInfo;

void test_ws_state_builder_setUp() {}
void test_ws_state_builder_tearDown() {}

namespace
{
DeviceState makeState()
{
    DeviceState state{};
    state.power = true;
    state.holding = false;
    state.brightness = 128;
    state.colorR = 255;
    state.colorG = 10;
    state.colorB = 20;
    state.colorActive = false;
    state.effectName = "Aurora";
    state.runningModel = ModelInfo{768, "L70"};
    state.configuredModel = ModelInfo{768, "L70"};
    state.fps = 42.5f;
    state.deviceUid = "A1B2";
    state.runningDeviceName = "Andromeda-A1B2";
    state.configuredDeviceName = "Andromeda-A1B2";
    return state;
}
}  // namespace

void test_power_true_round_trips()
{
    DeviceState state = makeState();
    state.power = true;
    char buf[WsStateBuilder::JSON_CAPACITY];
    size_t len = WsStateBuilder::buildStateJson(state, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, len);

    StaticJsonDocument<WsStateBuilder::JSON_CAPACITY> doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, buf, len));
    TEST_ASSERT_EQUAL_STRING("state", doc["type"]);
    TEST_ASSERT_TRUE(doc["power"].as<bool>());
}

void test_power_false_round_trips()
{
    DeviceState state = makeState();
    state.power = false;
    char buf[WsStateBuilder::JSON_CAPACITY];
    size_t len = WsStateBuilder::buildStateJson(state, buf, sizeof(buf));

    StaticJsonDocument<WsStateBuilder::JSON_CAPACITY> doc;
    deserializeJson(doc, buf, len);
    TEST_ASSERT_FALSE(doc["power"].as<bool>());
}

void test_holding_true_round_trips()
{
    DeviceState state = makeState();
    state.holding = true;
    char buf[WsStateBuilder::JSON_CAPACITY];
    size_t len = WsStateBuilder::buildStateJson(state, buf, sizeof(buf));

    StaticJsonDocument<WsStateBuilder::JSON_CAPACITY> doc;
    deserializeJson(doc, buf, len);
    TEST_ASSERT_TRUE(doc["holding"].as<bool>());
}

void test_holding_false_round_trips()
{
    DeviceState state = makeState();
    state.holding = false;
    char buf[WsStateBuilder::JSON_CAPACITY];
    size_t len = WsStateBuilder::buildStateJson(state, buf, sizeof(buf));

    StaticJsonDocument<WsStateBuilder::JSON_CAPACITY> doc;
    deserializeJson(doc, buf, len);
    TEST_ASSERT_FALSE(doc["holding"].as<bool>());
}

void test_color_boundary_values_and_active_flag()
{
    DeviceState state = makeState();
    state.colorR = 0;
    state.colorG = 255;
    state.colorB = 0;
    state.colorActive = false;
    char buf[WsStateBuilder::JSON_CAPACITY];
    size_t len = WsStateBuilder::buildStateJson(state, buf, sizeof(buf));

    StaticJsonDocument<WsStateBuilder::JSON_CAPACITY> doc;
    deserializeJson(doc, buf, len);
    TEST_ASSERT_EQUAL(0, doc["color"]["r"].as<int>());
    TEST_ASSERT_EQUAL(255, doc["color"]["g"].as<int>());
    TEST_ASSERT_EQUAL(0, doc["color"]["b"].as<int>());
    TEST_ASSERT_FALSE(doc["color"]["active"].as<bool>());
}

void test_effect_name_embedded_verbatim()
{
    DeviceState state = makeState();
    state.effectName = "Static Color";
    char buf[WsStateBuilder::JSON_CAPACITY];
    size_t len = WsStateBuilder::buildStateJson(state, buf, sizeof(buf));

    StaticJsonDocument<WsStateBuilder::JSON_CAPACITY> doc;
    deserializeJson(doc, buf, len);
    TEST_ASSERT_EQUAL_STRING("Static Color", doc["effect"]);
}

void test_reboot_required_false_when_models_match()
{
    DeviceState state = makeState();
    state.runningModel = ModelInfo{768, "L70"};
    state.configuredModel = ModelInfo{768, "L70"};
    char buf[WsStateBuilder::JSON_CAPACITY];
    size_t len = WsStateBuilder::buildStateJson(state, buf, sizeof(buf));

    StaticJsonDocument<WsStateBuilder::JSON_CAPACITY> doc;
    deserializeJson(doc, buf, len);
    TEST_ASSERT_FALSE(doc["model"]["rebootRequired"].as<bool>());
    TEST_ASSERT_EQUAL(768, doc["model"]["running"]["id"].as<int>());
    TEST_ASSERT_EQUAL(768, doc["model"]["configured"]["id"].as<int>());
}

void test_reboot_required_true_when_models_differ()
{
    DeviceState state = makeState();
    state.runningModel = ModelInfo{768, "L70"};
    state.configuredModel = ModelInfo{769, "L10"};
    char buf[WsStateBuilder::JSON_CAPACITY];
    size_t len = WsStateBuilder::buildStateJson(state, buf, sizeof(buf));

    StaticJsonDocument<WsStateBuilder::JSON_CAPACITY> doc;
    deserializeJson(doc, buf, len);
    TEST_ASSERT_TRUE(doc["model"]["rebootRequired"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("L10", doc["model"]["configured"]["name"]);
}

void test_models_array_matches_full_registry()
{
    DeviceState state = makeState();
    char buf[WsStateBuilder::JSON_CAPACITY];
    size_t len = WsStateBuilder::buildStateJson(state, buf, sizeof(buf));

    StaticJsonDocument<WsStateBuilder::JSON_CAPACITY> doc;
    deserializeJson(doc, buf, len);
    JsonArray models = doc["models"].as<JsonArray>();
    TEST_ASSERT_EQUAL(NUM_MODELS, models.size());

    for (size_t i = 0; i < NUM_MODELS; i++)
    {
        uint16_t id = models[i]["id"].as<uint16_t>();
        const ModelConfig* config = getModelConfig(static_cast<ModelId>(id));
        TEST_ASSERT_NOT_NULL(config);
        TEST_ASSERT_EQUAL_STRING(config->name, models[i]["name"]);
    }
}

void test_effects_array_matches_full_registry()
{
    DeviceState state = makeState();
    char buf[WsStateBuilder::JSON_CAPACITY];
    size_t len = WsStateBuilder::buildStateJson(state, buf, sizeof(buf));

    StaticJsonDocument<WsStateBuilder::JSON_CAPACITY> doc;
    deserializeJson(doc, buf, len);
    JsonArray effects = doc["effects"].as<JsonArray>();
    TEST_ASSERT_EQUAL(NUM_EFFECTS, effects.size());

    for (size_t i = 0; i < NUM_EFFECTS; i++)
    {
        TEST_ASSERT_EQUAL(static_cast<uint8_t>(EFFECT_REGISTRY[i].id),
                          effects[i]["id"].as<uint8_t>());
        TEST_ASSERT_EQUAL_STRING(EFFECT_REGISTRY[i].name, effects[i]["name"]);
    }
}

void test_device_fields_round_trip()
{
    DeviceState state = makeState();
    state.deviceUid = "F00D";
    state.runningDeviceName = "Andromeda-F00D";
    state.configuredDeviceName = "Andromeda-F00D";
    char buf[WsStateBuilder::JSON_CAPACITY];
    size_t len = WsStateBuilder::buildStateJson(state, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, len);

    StaticJsonDocument<WsStateBuilder::JSON_CAPACITY> doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, buf, len));
    TEST_ASSERT_EQUAL_STRING("F00D", doc["device"]["uid"]);
    TEST_ASSERT_EQUAL_STRING("Andromeda-F00D", doc["device"]["name"]);
    TEST_ASSERT_EQUAL_STRING("Andromeda-F00D", doc["device"]["runningName"]);
    TEST_ASSERT_FALSE(doc["device"]["nameRebootRequired"].as<bool>());
}

void test_device_name_reboot_required_true_when_names_differ()
{
    DeviceState state = makeState();
    state.runningDeviceName = "Andromeda-F00D";
    state.configuredDeviceName = "Kitchen-Lamp";
    char buf[WsStateBuilder::JSON_CAPACITY];
    size_t len = WsStateBuilder::buildStateJson(state, buf, sizeof(buf));

    StaticJsonDocument<WsStateBuilder::JSON_CAPACITY> doc;
    deserializeJson(doc, buf, len);
    TEST_ASSERT_TRUE(doc["device"]["nameRebootRequired"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("Kitchen-Lamp", doc["device"]["name"]);
}

void test_fps_serializes_as_number()
{
    DeviceState state = makeState();
    state.fps = 42.5f;
    char buf[WsStateBuilder::JSON_CAPACITY];
    size_t len = WsStateBuilder::buildStateJson(state, buf, sizeof(buf));

    StaticJsonDocument<WsStateBuilder::JSON_CAPACITY> doc;
    deserializeJson(doc, buf, len);
    TEST_ASSERT_TRUE(doc["fps"].is<float>());
    TEST_ASSERT_FLOAT_WITHIN(0.01, 42.5, doc["fps"].as<float>());
}

void run_test_ws_state_builder_tests()
{
    RUN_TEST(test_power_true_round_trips);
    RUN_TEST(test_power_false_round_trips);
    RUN_TEST(test_holding_true_round_trips);
    RUN_TEST(test_holding_false_round_trips);
    RUN_TEST(test_color_boundary_values_and_active_flag);
    RUN_TEST(test_effect_name_embedded_verbatim);
    RUN_TEST(test_reboot_required_false_when_models_match);
    RUN_TEST(test_reboot_required_true_when_models_differ);
    RUN_TEST(test_models_array_matches_full_registry);
    RUN_TEST(test_effects_array_matches_full_registry);
    RUN_TEST(test_device_fields_round_trip);
    RUN_TEST(test_device_name_reboot_required_true_when_names_differ);
    RUN_TEST(test_fps_serializes_as_number);
}
