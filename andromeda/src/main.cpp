#include "animations.h"
#include "comms.h"
#include "effects.h"
#include "energy-param.h"
#include "loggers.h"
#include "mission-control.h"
#include "perf-monitor.h"

#include <LittleFS.h>

void setup() {

  Serial.begin(115200);
  while(!Serial && !Serial.available()) {}
  LittleFS.begin();

  setupLoggers();

  initializeGeometry();
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