#include "mission-control.h"
#include "perf-monitor.h"
#include "comms.h"
#include "animations.h"
#include "effects.h"

MissionControl     mc    = MissionControl();
PerformanceMonitor perf  = PerformanceMonitor();
Comms              comms = Comms(mc);

void setup() {

  Serial.begin(115200);
  while(!Serial && !Serial.available()) {}
  Log.begin(LOG_LEVEL_VERBOSE, &Serial, true);

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
  mc.update(t);
  perf.tick();

  EVERY_N_MILLISECONDS(1000) {
    comms.loop();
  }

  EVERY_N_MILLISECONDS(5000) {
    perf.stat();
  }
}