#include <FastLED.h>

#include "utils.h"
#include "geometry.h"
#include "moodlight.h"
#include "effects.h"

#define PERF

void setup() {

#ifdef ARDUINO_UNOR4_WIFI
  Serial.begin(115200);
#else
  Serial.begin(9600);
#endif

  initializeGeometry();

  // Right now the mirrors are not attached and the leds are exposed,
  // and it's really fucking bright
  // TODO: hey look, another parameter where I can hook up a wave function!
  FastLED.setBrightness(180);

  // pin 0 is not attached to anything, so the voltage fluctuates
  // doing an analog read from it returns noise for the RNG
  randomSeed(analogRead(0));

  // TODO: these need to go
  for (byte i = 0; i < NUM_STRIPS; i++) {
    moodlights[i].randomize();
  }
}

LoopingPoint effect;

void loop() {

  unsigned long start = millis();

  // TODO: floating point time is not very MCU-friendly
  float t = millis() / 1000.0;

  CRGB color;
  color.r = 75;
  color.g = 1;
  color.b = 0;

  paint(color);

  effect.precompute(t);
  effect.render(STRIPS, t);

  float pulse = ssin(t / 2);
  pulse = pulse * pulse * pulse * pulse * pulse;
  uint8_t brightness = (uint8_t)(10.0 + (255.0 - 10.0) * pulse);
  FastLED.setBrightness(brightness);

  FastLED.show();

  unsigned long end = millis();

#ifdef PERF
  float duration = (float)(end - start) / 1000.0;
  float fps = 1.0 / duration;
  Serial.println(fps);
#endif
}