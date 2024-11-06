#include "control-logic.h"
#include "perf-monitor.h"

PerformanceMonitor perf = PerformanceMonitor();

void setup() {

  Serial.begin(115200);
  while(!Serial && !Serial.available()) {}
  Log.begin(LOG_LEVEL_VERBOSE, &Serial, true);

  initializeGeometry();
  FastLED.setCorrection(TypicalLEDStrip);
  seedRNGs();
}

void loop() {

  unsigned long t = millis();
  update(t);
  perf.tick();

  EVERY_N_MILLISECONDS(5000) {
    perf.stat();
  }
}