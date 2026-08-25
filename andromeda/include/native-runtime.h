#pragma once

#include "geometry/model_config.h"
#include "utils.h"

// Native-runtime-only glue: CLI model selection and real-time pacing for
// running the real MissionControl loop on the host with no ESP32 attached.
// Compiled only into env:native_runtime (see platformio.ini and
// src/main.cpp's NATIVE_RUNTIME branch); never linked into firmware builds.
namespace NativeRuntime
{

// Parses --model=<name> from argv (matched case-insensitively against each
// MODEL_REGISTRY entry's ModelConfig::name), caching the result for model()
// below. Falls back to ModelId::SINGLE_STRIP_TEST_DEVICE (the same default
// main.cpp uses for an unconfigured device) with a warning if no --model arg
// is given or it doesn't match a known model. Call once, before setup().
void init(int argc, char** argv);

// The model resolved by init().
ModelId model();

// Call once per main loop iteration, after MissionControl::update(). The
// FreeRTOS mock's vTaskDelay() is a no-op natively (test/mocks/freertos/
// FreeRTOS.h), so MissionControl::update()'s own frame-duration-cap delay
// doesn't actually sleep here - without this, the native loop free-spins at
// whatever speed the host CPU allows instead of the device's configured
// frame rate. This sleeps the real wall-clock remainder of frameDurationCapMs
// (falling back to ~60fps if the model doesn't set one) and throttles a
// simple stdout debug line to about once a second.
void tick(milliseconds_t frameDurationCapMs);

}  // namespace NativeRuntime
