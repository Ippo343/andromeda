#ifndef EFFECTS_UTILS_H
#define EFFECTS_UTILS_H

#include <FastLED.h>
#include "geometry.h"

// Fills every LED with the same color
void paint(CRGB color)
{
  FOR_EACH_STRIP {
    FOR_EACH_LED {
      STRIPS[iStrip].buffer[iLed] = color;
    }
  }
}

#endif