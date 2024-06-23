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

// TIL this is the professional approach to generating a random color.
// If you just generate a random RGB triplet the intensity will be all over the place,
// while HSV lets you generate random colors with the same intensity. Cool!
CRGB randomColor()
{
  return CHSV(random8(), 255, 255);
}

// Picks three random colors, equispaced around the HSV wheel
void threeRandomColors(CRGB* A, CRGB* B, CRGB*C)
{
  const short hueStep = 255 / 3;
  short a = random8();
  short b = (a + hueStep) % 255;
  short c = (b + hueStep) % 255;

  *A = CHSV(a, 255, 255);
  *B = CHSV(b, 255, 255);
  *C = CHSV(c, 255, 255);
}

#endif