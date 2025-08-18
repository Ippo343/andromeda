#include "mission-control.h"
#include "perf-monitor.h"
#include "comms.h"

#include "animations.h"
#include "effects.h"
#include "energy-param.h"

#include <LittleFS.h>

PerformanceMonitor perf  = PerformanceMonitor();
Comms              comms = Comms(MissionControl::Instance());

void setup() {

  Serial.begin(115200);
  while(!Serial && !Serial.available()) {}
  Log.begin(LOG_LEVEL_VERBOSE, &Serial, true);

  if (!LittleFS.begin())
  {
    Log.errorln("LittleFS Mount Failed");
    return;
  }

  initializeGeometry();
  FastLED.setCorrection(TypicalLEDStrip);
  seedRNGs();

  WiFiConnectingAnimation connecting;
  connecting.run();
  if (comms.setup())
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

  unsigned long t = millis();
  MissionControl::Instance().update(t);
  perf.tick();

  EVERY_N_MILLISECONDS(5000) {
    perf.stat();
  }
}