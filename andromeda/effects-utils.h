#ifndef EFFECTS_UTILS_H
#define EFFECTS_UTILS_H

#include <FastLED.h>
#include "geometry.h"
#include "utils.h"

// Fills every LED with the same color
void paint(CRGB color)
{
  FOR_EACH_STRIP {
    fill_solid(STRIPS[iStrip].buffer, LEDS_PER_STRIP, color);
  }
}

// Paints a single strip with the given color
// Thin wrapper around fill_solid because I'm that lazy
void paintStrip(byte idx, CRGB color)
{
  fill_solid(STRIPS[idx].buffer, LEDS_PER_STRIP, color);
}

#endif