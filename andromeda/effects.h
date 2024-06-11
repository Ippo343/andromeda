#ifndef EFFECTS_H
#define EFFECTS_H

#include <FastLED.h>
#include "utils.h"


MoodLight moodlights[NUM_STRIPS];

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

#endif