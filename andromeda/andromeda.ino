#include <FastLED.h>

#include "utils.h"
#include "geometry.h"
#include "moodlight.h"
#include "effects.h"


void setup() {

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


void loop() {

  // TODO: floating point time is not very MCU-friendly
  float t = millis() / 1000.0;

  CRGB color;
  color.r = 75;
  color.g = 1;
  color.b = 0;

  paint(color);

  float pulse = ssin(t / 2);
  pulse = pulse * pulse * pulse * pulse * pulse;
  uint8_t brightness = (uint8_t)(10.0 + (255.0 - 10.0) * pulse);
  FastLED.setBrightness(brightness);

  FastLED.show();
}