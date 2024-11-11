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


// Fade in a strip with the given color over the given duration
void fadeInStrip(byte idx, CHSV color, milliseconds duration)
{
  paintStrip(idx, CRGB::Black);
  FastLED.show();

  milliseconds start = millis();
  milliseconds dt = 0;
  do
  {
    dt = millis() - start;
    byte v = constrain(map(dt, 0, duration, 0, color.v), 0, 255);
    CHSV c = CHSV(color.h, color.s, v);
    paintStrip(idx, c);
    FastLED.show();
  }
  while (dt < duration);
}


// Fade in all strips together to the given color in the given duration
void fadeInAllStrips(CHSV color, milliseconds duration)
{
  paint(CRGB::Black);
  FastLED.show();

  milliseconds start = millis();
  milliseconds dt = 0;
  do
  {
    dt = millis() - start;
    byte v = map(dt, 0, duration, 0, color.v);
    CHSV c = CHSV(color.h, color.s, v);
    FOR_EACH_STRIP {
      paintStrip(iStrip, c);
    }
    FastLED.show();
  }
  while (dt < duration);
}


// TIL this is the professional approach to generating a random color.
// If you just generate a random RGB triplet the intensity will be all over the place,
// while HSV lets you generate random colors with the same intensity. Cool!
CHSV randomColor()
{
  return CHSV(random8(), 255, 255);
}


// Picks three random colors, equispaced around the HSV wheel
void randomComplementaryColors(CRGB* A, CRGB* B, CRGB*C)
{
  const short hueStep = 255 / 3;
  short a = random8();
  short b = (a + hueStep) % 255;
  short c = (b + hueStep) % 255;

  *A = CHSV(a, 255, 255);
  *B = CHSV(b, 255, 255);
  *C = CHSV(c, 255, 255);
}

// Picks three random colors, equispaced around the HSV wheel
void randomComplementaryColors(CRGB* A, CRGB* B)
{
  const short hueStep = 255 / 2;
  short a = random8();
  short b = (a + hueStep) % 255;

  *A = CHSV(a, 255, 255);
  *B = CHSV(b, 255, 255);
}


CRGBPalette16 randomPredefinedPalette()
{
  CRGBPalette16 retval;
  switch (random(8))
  {
    case 0:
      retval = CloudColors_p;
      break;
    case 1:
      retval = LavaColors_p;
      break;
    case 2:
      retval = OceanColors_p;
      break;
    case 3:
      retval = ForestColors_p;
      break;
    case 4:
      retval = RainbowColors_p;
      break;
    case 5:
      retval = RainbowStripeColors_p;
      break;
    case 6:
      retval = PartyColors_p;
      break;
    case 7:
      retval = HeatColors_p;
      break;
  }

  return retval;
}

#endif
