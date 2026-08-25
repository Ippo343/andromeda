#pragma once

#include <ArduinoJson.h>

#include <cstring>

#include "effects.h"
#include "geometry/model_config.h"
#include "geometry/model_registry.h"

// Pure, dependency-light builder for the "state" JSON message broadcast to
// web clients over /ws. Only depends on ArduinoJson + the geometry model
// headers (no Arduino String, no WiFi/networking), so it's natively
// unit-testable, mirroring ws-command-parser.h's extraction (comms.cpp
// itself stays excluded from most native builds - see platformio.ini
// [env:native]).

namespace WsStateBuilder
{

struct ModelInfo
{
    uint16_t id;
    const char* name;
};

// Plain snapshot of everything the state broadcast needs. Built by Comms
// from MissionControl/GEOMETRY/FactoryConfig/PerformanceMonitor; kept as a
// POD so this header has no Arduino/singleton dependencies of its own.
struct DeviceState
{
    bool power;
    bool holding;
    uint8_t brightness;
    uint8_t colorR, colorG, colorB;
    bool colorActive;
    const char* effectName;
    ModelInfo runningModel;
    ModelInfo configuredModel;
    float fps;
    const char* deviceUid;
    const char* runningDeviceName;
    const char* configuredDeviceName;
};

// StaticJsonDocument capacity for the schema below plus the full model and
// effect registries. Fixed, zero-heap - bump this if fields/models/effects
// are added.
constexpr size_t JSON_CAPACITY = 3072;

// Serializes `state` plus the full MODEL_REGISTRY into `outBuffer` (of size
// `outBufferSize`). Returns the number of bytes written (0 on failure, e.g.
// if the document overflowed its fixed capacity), mirroring ArduinoJson's
// own serializeJson(doc, char*, size_t) return convention.
inline size_t buildStateJson(const DeviceState& state, char* outBuffer, size_t outBufferSize)
{
    StaticJsonDocument<JSON_CAPACITY> doc;

    doc["type"] = "state";
    doc["power"] = state.power;
    doc["holding"] = state.holding;
    doc["brightness"] = state.brightness;

    JsonObject color = doc.createNestedObject("color");
    color["r"] = state.colorR;
    color["g"] = state.colorG;
    color["b"] = state.colorB;
    color["active"] = state.colorActive;

    doc["effect"] = state.effectName;

    JsonObject model = doc.createNestedObject("model");
    JsonObject running = model.createNestedObject("running");
    running["id"] = state.runningModel.id;
    running["name"] = state.runningModel.name;
    JsonObject configured = model.createNestedObject("configured");
    configured["id"] = state.configuredModel.id;
    configured["name"] = state.configuredModel.name;
    model["rebootRequired"] = (state.runningModel.id != state.configuredModel.id);

    JsonArray models = doc.createNestedArray("models");
    for (size_t i = 0; i < NUM_MODELS; i++)
    {
        JsonObject m = models.createNestedObject();
        m["id"] = static_cast<uint16_t>(MODEL_REGISTRY[i]->id);
        m["name"] = MODEL_REGISTRY[i]->name;
    }

    JsonArray effects = doc.createNestedArray("effects");
    for (size_t i = 0; i < NUM_EFFECTS; i++)
    {
        JsonObject e = effects.createNestedObject();
        e["id"] = static_cast<uint8_t>(EFFECT_REGISTRY[i].id);
        e["name"] = EFFECT_REGISTRY[i].name;
    }

    doc["fps"] = state.fps;

    JsonObject device = doc.createNestedObject("device");
    device["uid"] = state.deviceUid;
    device["name"] = state.configuredDeviceName;
    device["runningName"] = state.runningDeviceName;
    device["nameRebootRequired"] =
        (strcmp(state.runningDeviceName, state.configuredDeviceName) != 0);

    if (doc.overflowed()) return 0;
    return serializeJson(doc, outBuffer, outBufferSize);
}

}  // namespace WsStateBuilder
