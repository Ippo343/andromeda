#include <FastLED.h>

#define PERF

#include "utils.h"
#include "geometry.h"
#include "moodlight.h"
#include "effects.h"

#define MIN_BRIGHTNESS 25
#define MAX_BRIGHTNESS 75

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
  FastLED.setBrightness(MAX_BRIGHTNESS);

  // The analog pins not attached to anything, so the voltage fluctuates
  // doing an analog read from it returns noise for the RNG
  randomSeed(analogRead(0));
  delay(10);
  random16_set_seed(analogRead(1));
  delay(10);
  random16_add_entropy(analogRead(2));

  // TODO: these need to go
  for (byte i = 0; i < NUM_STRIPS; i++) {
    moodlights[i].randomize();
  }
}

LoopingPoint effect;

void loop() {

  unsigned long t = millis();

  effect.precompute(t);
  effect.render(STRIPS, t);
  effect.postprocess(t);

  // TODO: disabled while I figure out the integer sin functions
  // float pulse = ssin(t / 2);
  // pulse = pulse * pulse * pulse * pulse * pulse;
  // uint8_t brightness = (uint8_t)(MIN_BRIGHTNESS + (MAX_BRIGHTNESS - MIN_BRIGHTNESS) * pulse);
  // FastLED.setBrightness(brightness);

  FastLED.show();

  unsigned long end = millis();

#ifdef PERF
  float fps = 1000.0 / (float)(end - t);
  Serial.println(fps);
#endif
}