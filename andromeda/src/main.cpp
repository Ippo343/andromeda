#include "animations.h"
#include "comms.h"
#include "effects.h"
#include "energy-param.h"
#include "loggers.h"
#include "mission-control.h"
#include "perf-monitor.h"
#include "geometry/geometry.h"
#include <LittleFS.h>

void setup() {
  Serial.begin(115200);
  while(!Serial && !Serial.available()) {}

  // Wait a little bit to allow the PC to start the serial monitor before we start spamming it with logs
  delay(50);

  LittleFS.begin();
  setupLoggers();
  Log.noticeln("=== Andromeda Device Starting Up ===");

  // Load model ID
  // TODO: selection via web UI, currently hardcoded to SINGLE_STRIP_TEST_DEVICE for testing purposes
  ModelId model;
  if (FactoryConfig::isConfigured()) {
    model = FactoryConfig::getModelId();
    Log.noticeln("Loading factory configuration: %s", getModelName(model));
  } else {
    model = ModelId::SINGLE_STRIP_TEST_DEVICE;
    Log.warningln("Device not factory configured, using default: %s", getModelName(model));
  }

  GEOMETRY.initialize(model);

  // Log device configuration
  const ModelConfig* config = GEOMETRY.getConfig();
  Log.noticeln("Device: %s (%d strips, %d mm screen)",
               config->name,
               config->num_strips,
               config->screen_size_mm);

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
}

void loop() {
  MissionControl::Instance().update(millis());
}
