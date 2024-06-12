#include <FastLED.h>

#include "utils.h"
#include "geometry.h"
#include "moodlight.h"
#include "effects.h"


// the setup function runs once when you press reset or power the board
void setup() {

  initializeGeometry();

  FastLED.setBrightness(180);

  randomSeed(analogRead(0));

  for (byte i = 0; i < NUM_STRIPS; i++) {
    moodlights[i].randomize();
  }
}


// the loop function runs over and over again forever
void loop() {

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