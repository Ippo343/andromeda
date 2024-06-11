#ifndef ANDROMEDA_GEOMETRY_H
#define ANDROMEDA_GEOMETRY_H

#include <FastLED.h>
#include "utils.h"

// Data related to the geometry of the mirror,
// e.g. dimensions, number of leds, and so on

const byte LEDS_PER_STRIP = 23;
const byte NUM_STRIPS = 7;

CRGB STRIPS[NUM_STRIPS][LEDS_PER_STRIP];

void initializePins() {
  // Setup FastLED to map each strip's pin to the corresponding color buffer
  // Note that because this is a template method you cannot use a loop,
  // the pin number must be a compile-time constant
  FastLED.addLeds<WS2812B, 1, GRB>(STRIPS[0], LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, 2, GRB>(STRIPS[1], LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, 3, GRB>(STRIPS[2], LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, 4, GRB>(STRIPS[3], LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, 5, GRB>(STRIPS[4], LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, 6, GRB>(STRIPS[5], LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, 7, GRB>(STRIPS[6], LEDS_PER_STRIP);
}

#endif