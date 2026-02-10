#pragma once

#include <FastLED.h>

#include <vector>
using std::vector;

#include "geometry/geometry.h"
#include "perf-monitor.h"
#include "utils.h"

// Fills every LED with the same color
void paint(CRGB color);

// Paints a single strip with the given color
// Thin wrapper around fill_solid because I'm that lazy
void paintStrip(int idx, CRGB color);

// Fade in a strip with the given color over the given duration
void fadeInStrip(int idx, CHSV color, milliseconds_t duration);

// Fade in all strips together to the given color in the given duration
void fadeInAllStrips(CHSV color, milliseconds_t duration);

// TIL this is the professional approach to generating a random color.
// If you just generate a random RGB triplet the intensity will be all over the place,
// while HSV lets you generate random colors with the same intensity. Cool!
CHSV randomColor();

// Generate N random complementary colors spaced evenly on the hue wheel
vector<CHSV> randomComplementaryColors(int N);

// Pick a random predefined palette
CRGBPalette16 randomPredefinedPalette();

// This factor control the final brightness of each channel.
// 255 is obviously the maximum brightness: but then you need to multiply but some factor
// because otherwise (255 / d^2) is always very very dim.
// I found 5000 by trial and error and it looks good.
extern const float defaultBrightnessFactor;

// Compute brightness based on emitter position and a brightness factor.
// Uses the famous fast inverse square root for speed.
// Physically correct (1/d^2) looks cooler than just inverse distance.
uint8_t brightnessFromEmitter(Led* led, CartesianCoordinates e,
                              float brightnessFactor = defaultBrightnessFactor);
