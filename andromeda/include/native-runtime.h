#pragma once

#include "geometry/model_config.h"
#include "utils.h"

// Native-runtime-only glue: CLI model selection, the stdio JSON protocol
// spoken to tools/native-bridge/server.js, and real-time pacing for running
// the real MissionControl loop on the host with no ESP32 attached. Compiled
// only into env:native_runtime (see platformio.ini and src/main.cpp's
// NATIVE_RUNTIME branch); never linked into firmware builds.
//
// Wire protocol (newline-delimited JSON, both directions over stdio):
//   stdin  (bridge -> core): the same command JSON the browser already sends
//     over /ws on real hardware (color/brightness/effect/model/etc.), fed
//     straight into WsCommandParser::parse()/parseBrightness() unmodified.
//   stdout (core -> bridge): {"type":"state",...} (WsStateBuilder::
//     buildStateJson's output, whenever MissionControl::consumeStateDirty()
//     is true), {"type":"frame","leds":[[r,g,b],...]} (once per real
//     FASTLED_SHOW() tick), and {"type":"geometry",...} (once, right after
//     boot).
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

// Call once, right after GEOMETRY.initializeForTest(model()) in setup():
// installs the FASTLED_SHOW() frame-capture hook (perf-monitor.h) and emits
// the one-time {"type":"geometry",...} message. Must run before the stdin
// reader thread's commands could plausibly reach MissionControl, but in
// practice just needs to run once, early, after geometry is loaded.
void installProtocol();

// Call once per main loop iteration, after MissionControl::update(). Drains
// any command lines that arrived on stdin since the last call (dispatching
// them into MissionControl exactly like comms.cpp's WS_EVT_DATA handler
// does), emits a fresh {"type":"state",...} line if state changed, and then
// paces the loop to frameDurationCapMs of real wall-clock time - the
// FreeRTOS mock's vTaskDelay() is a no-op natively (test/mocks/freertos/
// FreeRTOS.h), so MissionControl::update()'s own frame-duration-cap delay
// doesn't actually sleep here; without this the native loop would free-spin
// at whatever speed the host CPU allows instead of the device's configured
// frame rate.
void tick(milliseconds_t frameDurationCapMs);

}  // namespace NativeRuntime
