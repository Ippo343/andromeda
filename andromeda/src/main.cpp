#include "mission-control.h"
#include "perf-monitor.h"

#ifdef ARDUINO_R4_WIFI
#include "comms.h"
#endif

#include "animations.h"
#include "effects.h"
#include "energy-param.h"

PerformanceMonitor perf  = PerformanceMonitor();

#ifdef ARDUINO_R4_WIFI
Comms              comms = Comms(MissionControl::Instance());
#endif

void setup() {

  Serial.begin(115200);
  while(!Serial && !Serial.available()) {}
  Log.begin(LOG_LEVEL_VERBOSE, &Serial, true);

  initializeGeometry();
  FastLED.setCorrection(TypicalLEDStrip);
  seedRNGs();

#ifdef ARDUINO_R4_WIFI
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
#endif
}

void loop() {

  unsigned long t = millis();
  MissionControl::Instance().update(t);
  perf.tick();

#ifdef ARDUINO_R4_WIFI
  EVERY_N_MILLISECONDS(1000) {
    comms.loop();
  }
#endif

  EVERY_N_MILLISECONDS(5000) {
    perf.stat();
  }
}