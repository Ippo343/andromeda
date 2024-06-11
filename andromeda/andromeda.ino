#include <FastLED.h>

#include "utils.h"
#include "geometry.h"
#include "moodlight.h"

MoodLight moodlights[NUM_STRIPS];

// the setup function runs once when you press reset or power the board
void setup() {

  FastLED.addLeds<WS2812B, 1, GRB>(STRIPS[0], LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, 2, GRB>(STRIPS[1], LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, 3, GRB>(STRIPS[2], LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, 4, GRB>(STRIPS[3], LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, 5, GRB>(STRIPS[4], LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, 6, GRB>(STRIPS[5], LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, 7, GRB>(STRIPS[6], LEDS_PER_STRIP);

  FastLED.setBrightness(180);

  randomSeed(analogRead(0));

  for (byte i = 0; i < NUM_STRIPS; i++) {
    moodlights[i].Randomize();
  }
}

// Fills every LED with the same color
void paint(CRGB color)
{
  FOR_EACH_STRIP {
    FOR_EACH_LED {
      STRIPS[strip][led] = color;
    }
  }
}

void chaos_moodlight(float t)
{
  FOR_EACH_STRIP {
    for (int led = 0; led < LEDS_PER_STRIP; led = led + 1) {
        STRIPS[strip][led] = moodlights[strip].Evaluate(led, t);
      }
  }
}

void whole_moodlight(float t)
{
  CRGB color = moodlights[0].Evaluate(0, t);
  paint(color);
}

void individual_moodlight(float t)
{
  FOR_EACH_STRIP {
    for (byte led = 0; led < LEDS_PER_STRIP; led++) {
        STRIPS[strip][led] = moodlights[strip].Evaluate(0, t);
    }
  }
}

void looping_point(float t)
{
  CRGB color = moodlights[0].Evaluate(0, t);

  int idx = (int)(t / 100.0) % (LEDS_PER_STRIP / 2);

  // Index of the symmetrically opposite LED (if you want two points)
  int sidx = idx + (LEDS_PER_STRIP / 2);
  
  paint(CRGB::Black);

  FOR_EACH_STRIP {
    STRIPS[strip][idx] = color;
    // STRIPS[strip][sidx] = color;
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