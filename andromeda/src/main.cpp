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

    // To avoid burning my eyes while working at the Social Hub's desk
    MissionControl::Instance().setMaxBrightness(64);

    MissionControl::Instance().setMaxCpuFrequency(config->max_cpu_freq_mhz);
}

void loop() { MissionControl::Instance().update(millis()); }
