#include "control-logic.h"

//#define PERF

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

#ifdef PERF
  unsigned long end = millis();
  float fps = 1000.0 / (float)(end - t);
  Serial.println(fps);
#endif
}