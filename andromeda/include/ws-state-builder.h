#pragma once

#include <ArduinoJson.h>

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
    uint8_t brightness;
    uint8_t colorR, colorG, colorB;
    bool colorActive;
    const char* effectName;
    ModelInfo runningModel;
    ModelInfo configuredModel;
    float fps;
};

// StaticJsonDocument capacity for the schema below plus the full model
// registry. Fixed, zero-heap - bump this if fields/models are added.
constexpr size_t JSON_CAPACITY = 1536;

// Serializes `state` plus the full MODEL_REGISTRY into `outBuffer` (of size
// `outBufferSize`). Returns the number of bytes written (0 on failure, e.g.
// if the document overflowed its fixed capacity), mirroring ArduinoJson's
// own serializeJson(doc, char*, size_t) return convention.
inline size_t buildStateJson(const DeviceState& state, char* outBuffer, size_t outBufferSize)
{
    StaticJsonDocument<JSON_CAPACITY> doc;

    doc["type"] = "state";
    doc["power"] = state.power;
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

    doc["fps"] = state.fps;

    if (doc.overflowed()) return 0;
    return serializeJson(doc, outBuffer, outBufferSize);
}

}  // namespace WsStateBuilder
