#include "animations.h"
#include "comms.h"
#include "effects.h"
#include "energy-param.h"
#include "loggers.h"
#include "mission-control.h"
#include "perf-monitor.h"
#include "geometry.h"
#include <LittleFS.h>

void setup() {
  Serial.begin(115200);
  while(!Serial && !Serial.available()) {}

  LittleFS.begin();
  setupLoggers();

  // Initialize geometry with factory configuration
  ModelId model;
  if (FactoryConfig::isConfigured()) {
    model = FactoryConfig::getModelId();
    Log.noticeln("Loading factory configuration: %s", getModelName(model));
  } else {
    Log.warningln("Device not factory configured, using default: Andromeda Mk1");
    model = ModelId::ANDROMEDA_MK1;
    // Optionally set it for future boots:
    // FactoryConfig::setModelId(model);
  }

  GEOMETRY.initialize(model);

  if (!GEOMETRY.isInitialized()) {
    Log.errorln("Failed to initialize geometry!");
    while (1) { delay(1000); }
  }

  // Log device configuration
  const ModelConfig* config = GEOMETRY.getConfig();
  Log.noticeln("Device: %s %s (%d strips, %d mm screen)",
               config->family, config->model_name,
               config->num_strips, config->screen_size_mm);

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
}

void loop() {
  MissionControl::Instance().update(millis());
}
