#include <LittleFS.h>

#include "animations.h"
#include "comms.h"
#include "effects.h"
#include "energy-param.h"
#include "geometry/geometry.h"
#ifndef NATIVE_RUNTIME
// Real Serial/file logging via ArduinoLog's Print API - meaningless on the
// host, where Log is instead the no-op LoggingStub mock (test/mocks/
// ArduinoLog.h) that every Log.*ln() call below already degrades to.
#include "loggers.h"
#endif
#include "mission-control.h"
#include "perf-monitor.h"
#include "version.h"

#ifdef NATIVE_RUNTIME
#include "native-runtime.h"
#endif

void setup()
{
#ifndef NATIVE_RUNTIME
    Serial.begin(115200);
    while (!Serial && !Serial.available()) {}

    // Wait a little bit to allow the PC to start the serial monitor before we start spamming it
    // with logs
    delay(100);

    LittleFS.begin();
    setupLoggers();
#endif
    Log.noticeln("=== Andromeda Device Starting Up ===");
    Log.noticeln("=== Version: %s", VERSION);
    Log.noticeln("====================================");

    // Load model ID
    ModelId model;
#ifdef NATIVE_RUNTIME
    // No factory NVS on the host - resolved once from argv by
    // NativeRuntime::init(), called before setup() in main() below.
    model = NativeRuntime::model();
#else
    if (FactoryConfig::isConfigured())
    {
        model = FactoryConfig::getModelId();
        Log.noticeln("Loading factory configuration: %s", getModelName(model));
    }
    else
    {
        model = ModelId::SINGLE_STRIP_TEST_DEVICE;
        Log.warningln("Device not factory configured, using default: %s", getModelName(model));
    }
#endif

#ifdef NATIVE_RUNTIME
    // Skips bindHardwareDrivers() (the FastLED.addLeds<...> calls) - there's
    // no real/simulated LED hardware to bind to on the host.
    GEOMETRY.initializeForTest(model);
    // Must run right after geometry loads and before anything can rotate it
    // (see native-runtime.cpp's emitGeometryOnce() for why that ordering
    // matters) - installs the frame-capture hook and emits the one-time
    // geometry message for the visualizer bridge.
    NativeRuntime::installProtocol();
#else
    GEOMETRY.initialize(model);
#endif

    // Log device configuration
    const ModelConfig* config = GEOMETRY.getConfig();
    Log.noticeln("Device: %s", config->name);

    FastLED.setCorrection(TypicalLEDStrip);
    seedRNGs();

    // Restore the last brightness configured via the web UI before running
    // any boot-status indicator below. WiFiConnectingAnimation/
    // WiFiSuccessAnimation/ErrorAnimation call FASTLED_SHOW() directly
    // rather than going through MissionControl::update() (which hasn't
    // started yet), so without this they always render at FastLED's
    // power-on default (full brightness) regardless of what's configured -
    // the very first thing the device ever lights up ignored the stored
    // value. 64 is the fallback for a never-configured device, to avoid
    // burning my eyes while working at the Social Hub's desk.
    uint8_t maxBrightness = BrightnessConfig::load(64);
    MissionControl::Instance().setMaxBrightness(maxBrightness);
    FastLED.setBrightness(dim8_raw(maxBrightness));

#ifndef NATIVE_RUNTIME
    WiFiConnectingAnimation connecting;
    connecting.run();

    if (Comms::Instance().setup())
    {
        WiFiSuccessAnimation success;
        success.run();
    }
    else
    {
        ErrorAnimation error;
        error.run();
    }
#endif

    MissionControl::Instance().setCpuFrequency(config->preferred_cpu_freq_mhz);
    MissionControl::Instance().setFrameDurationCap(config->min_frame_duration_ms);
}

#ifdef NATIVE_RUNTIME
void loop()
{
    MissionControl::Instance().update(millis());
    NativeRuntime::tick(GEOMETRY.getConfig()->min_frame_duration_ms);
}

int main(int argc, char** argv)
{
    NativeRuntime::init(argc, argv);
    setup();
    while (true) loop();
    return 0;
}
#else
void loop() { MissionControl::Instance().update(millis()); }
#endif
