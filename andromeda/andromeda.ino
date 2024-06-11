#include <FastLED.h>

#include "utils.h"
#include "geometry.h"
#include "moodlight.h"

MoodLight moodlights[NUM_STRIPS];

// the setup function runs once when you press reset or power the board
void setup() {

  initializePins();

  FastLED.setBrightness(180);

  randomSeed(analogRead(0));

  for (byte i = 0; i < NUM_STRIPS; i++) {
    moodlights[i].randomize();
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

void chaosMoodlight(float t)
{
  FOR_EACH_STRIP {
    for (int led = 0; led < LEDS_PER_STRIP; led = led + 1) {
        STRIPS[strip][led] = moodlights[strip].evaluate(led, t);
      }
  }
}

void wholeMoodlight(float t)
{
  CRGB color = moodlights[0].evaluate(0, t);
  paint(color);
}

void individualMoodlight(float t)
{
  FOR_EACH_STRIP {
    for (byte led = 0; led < LEDS_PER_STRIP; led++) {
        STRIPS[strip][led] = moodlights[strip].evaluate(0, t);
    }
  }
}

void loopinPoint(float t)
{
  CRGB color = moodlights[0].evaluate(0, t);

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