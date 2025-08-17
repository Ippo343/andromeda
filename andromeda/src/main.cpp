#include "mission-control.h"
#include "perf-monitor.h"
#include "comms.h"

#include "animations.h"
#include "effects.h"
#include "energy-param.h"

PerformanceMonitor perf  = PerformanceMonitor();
Comms              comms = Comms(MissionControl::Instance());

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
#ifdef ESP32
    MissionControl::Instance().initWebQueue();
#endif
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

#ifdef ARDUINO_R4_WIFI
  EVERY_N_MILLISECONDS(1000) {
    comms.loop();
  }
#endif

#ifdef ESP32
  MissionControl::Instance().processWebCommands();
#endif

  EVERY_N_MILLISECONDS(5000) {
    perf.stat();
  }
}