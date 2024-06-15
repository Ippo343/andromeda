#ifndef COLOR_PALETTES_H
#define COLOR_PALETTES_H

#include <FastLED.h>

DEFINE_GRADIENT_PALETTE (red_sparks_gp) {
    0,  50,   0,   0, // dark
   25, 255,   0,   0, // full
   50, 255, 100, 100, // bright
  255, 255, 255, 255  // white
};

DEFINE_GRADIENT_PALETTE (green_sparks_gp) {
    0,   0,   50,  0, // dark
   25,   0, 255,   0, // full
   50, 100, 255, 100, // bright
  255, 255, 255, 255  // white
};

DEFINE_GRADIENT_PALETTE (blue_sparks_gp) {
    0,   0,   0,  50, // dark
   25,   0,   0, 255, // full
   50, 100, 100, 255, // bright
  255, 255, 255, 255  // white
};

#endif