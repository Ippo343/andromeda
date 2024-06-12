#ifndef ANDROMEDA_GEOMETRY_H
#define ANDROMEDA_GEOMETRY_H

// Data related to the geometry of the mirror,
// e.g. dimensions, number of leds, and so on

#include <FastLED.h>
#include "utils.h"

const byte LEDS_PER_STRIP = 23;
const byte NUM_STRIPS = 7;

// Represents the geometric information for a single LED
// For the moment it only stores its index in the strip,
// but in the future it will be extended to include its coordinates.
struct Led
{
  public:
    byte idx;
};


// Data releated to a single strip.
// Contains the data for all its leds
// and the color buffer for rendering.
class LedStrip
{
  public:
    // geometry info for each led in the strip
    Led leds[LEDS_PER_STRIP];

    // color buffer for rendering
    CRGB buffer[LEDS_PER_STRIP];

    LedStrip() {
      // Initialize each led with its index in the strip
      FOR_EACH_LED {
        this->leds[led].idx = led;
      }
    }
};


// The actual strips the program is controlling
LedStrip STRIPS[NUM_STRIPS];


void initializePins() {
  // Setup FastLED to map each strip's pin to the corresponding color buffer
  // Note that because this is a template method you cannot use a loop,
  // the pin number must be a compile-time constant
  FastLED.addLeds<WS2812B, 1, GRB>(STRIPS[0].buffer, LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, 2, GRB>(STRIPS[1].buffer, LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, 3, GRB>(STRIPS[2].buffer, LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, 4, GRB>(STRIPS[3].buffer, LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, 5, GRB>(STRIPS[4].buffer, LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, 6, GRB>(STRIPS[5].buffer, LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, 7, GRB>(STRIPS[6].buffer, LEDS_PER_STRIP);
}

#endif
