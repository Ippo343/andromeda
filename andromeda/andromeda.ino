#include <FastLED.h>

#include "utils.h"
#include "geometry.h"
#include "moodlight.h"

CRGB strip0[LEDS_PER_STRIP];
CRGB strip1[LEDS_PER_STRIP];
CRGB strip2[LEDS_PER_STRIP];
CRGB strip3[LEDS_PER_STRIP];
CRGB strip4[LEDS_PER_STRIP];
CRGB strip5[LEDS_PER_STRIP];
CRGB strip6[LEDS_PER_STRIP];

CRGB test_colors[4];
unsigned long period = 2500;

MoodLight moodlight_0;
MoodLight moodlight_1;
MoodLight moodlight_2;
MoodLight moodlight_3;
MoodLight moodlight_4;
MoodLight moodlight_5;
MoodLight moodlight_6;

// the setup function runs once when you press reset or power the board
void setup() {

  FastLED.addLeds<WS2812B,  1, GRB>(strip0, LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B,  2, GRB>(strip1, LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B,  3, GRB>(strip2, LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B,  4, GRB>(strip3, LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B,  5, GRB>(strip4, LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B,  6, GRB>(strip5, LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B,  7, GRB>(strip6, LEDS_PER_STRIP);
  
  FastLED.setBrightness(180);

  randomSeed(analogRead(0));

  moodlight_0.Randomize();
  moodlight_1.Randomize();
  moodlight_2.Randomize();
  moodlight_3.Randomize();
  moodlight_4.Randomize();
  moodlight_5.Randomize();
  moodlight_6.Randomize();
}

void chaos_moodlight(float t)
{
  for (int led = 0; led < LEDS_PER_STRIP; led = led + 1) {
    strip0[led] = moodlight_0.Evaluate(led, t);
    strip1[led] = moodlight_1.Evaluate(led, t);
    strip2[led] = moodlight_2.Evaluate(led, t);
    strip3[led] = moodlight_3.Evaluate(led, t);
    strip4[led] = moodlight_4.Evaluate(led, t);
    strip5[led] = moodlight_5.Evaluate(led, t);
    strip6[led] = moodlight_6.Evaluate(led, t);
  }
}

void whole_moodlight(float t)
{
  CRGB color = moodlight_0.Evaluate(0, t);
  for (int led = 0; led < LEDS_PER_STRIP; led = led + 1) {
    strip0[led] = color;
    strip1[led] = color;
    strip2[led] = color;
    strip3[led] = color;
    strip4[led] = color;
    strip5[led] = color;
    strip6[led] = color;
  }
}

void individual_moodlight(float t)
{
  CRGB color = moodlight_0.Evaluate(0, t);
  for (int led = 0; led < LEDS_PER_STRIP; led = led + 1) {
    strip0[led] = moodlight_0.Evaluate(0, t);
    strip1[led] = moodlight_1.Evaluate(0, t);
    strip2[led] = moodlight_2.Evaluate(0, t);
    strip3[led] = moodlight_3.Evaluate(0, t);
    strip4[led] = moodlight_4.Evaluate(0, t);
    strip5[led] = moodlight_5.Evaluate(0, t);
    strip6[led] = moodlight_6.Evaluate(0, t);
  }
}

void loop(float t)
{
  int idx = (int)(t / 100.0) % (LEDS_PER_STRIP / 2);
  int sidx = idx + (LEDS_PER_STRIP / 2);
  CRGB color = moodlight_0.Evaluate(0, t);

  for (int led = 0; led < LEDS_PER_STRIP; led = led + 1) {
    strip0[led] = CRGB::Black;
    strip1[led] = CRGB::Black;
    strip2[led] = CRGB::Black;
    strip3[led] = CRGB::Black;
    strip4[led] = CRGB::Black;
    strip5[led] = CRGB::Black;
    strip6[led] = CRGB::Black;
  }

    strip0[idx] = color;
    strip1[idx] = color;
    strip2[idx] = color;
    strip3[idx] = color;
    strip4[idx] = color;
    strip5[idx] = color;
    strip6[idx] = color;

    strip0[sidx] = color;
    strip1[sidx] = color;
    strip2[sidx] = color;
    strip3[sidx] = color;
    strip4[sidx] = color;
    strip5[sidx] = color;
    strip6[sidx] = color;
}

void paint(CRGB color)
{
  for (int led = 0; led < LEDS_PER_STRIP; led = led + 1) {
    strip0[led] = color;
    strip1[led] = color;
    strip2[led] = color;
    strip3[led] = color;
    strip4[led] = color;
    strip5[led] = color;
    strip6[led] = color;
  }
}

// the loop function runs over and over again forever
void loop() {

  float t = millis() / 1000.0;

  //loop(t);

  CRGB color;
  color.r = 75;
  color.g = 1;
  color.b = 0;

  //color = moodlight_0.Evaluate(0, t);
  paint(color);

  float pulse = ssin(t / 2);
  pulse = pulse * pulse * pulse * pulse * pulse;
  uint8_t brightness = (uint8_t)(10.0 + (255.0 - 10.0) * pulse);
  FastLED.setBrightness(brightness);

  FastLED.show();
}
