#include <LittleFS.h>

#include "animations.h"
#include "comms.h"
#include "effects.h"
#include "energy-param.h"
#include "geometry/geometry.h"
#include "loggers.h"
#include "mission-control.h"
#include "perf-monitor.h"
#include "version.h"

void setup()
{
    Serial.begin(115200);
    while (!Serial && !Serial.available()) {}

    // Wait a little bit to allow the PC to start the serial monitor before we start spamming it
    // with logs
    delay(100);

    LittleFS.begin();
    setupLoggers();
    Log.noticeln("=== Andromeda Device Starting Up ===");
    Log.noticeln("=== Version: %s", VERSION);
    Log.noticeln("====================================");

    // Load model ID
    ModelId model;
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

    GEOMETRY.initialize(model);

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

    MissionControl::Instance().setCpuFrequency(config->preferred_cpu_freq_mhz);
    MissionControl::Instance().setFrameDurationCap(config->min_frame_duration_ms);
}

void loop() { MissionControl::Instance().update(millis()); }
