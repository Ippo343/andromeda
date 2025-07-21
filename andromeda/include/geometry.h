#ifndef ANDROMEDA_GEOMETRY_H
#define ANDROMEDA_GEOMETRY_H

// Data related to the geometry of the mirror,
// e.g. dimensions, number of leds, and so on


// Data cable connections are as follows:
//
//         1
//       /¯¯\           0: yellow
// 6 /¯¯\\__//¯¯\ 2     1: also yellow, but from below the structure (we ran out of colors)
//   \__//¯¯\\__/       2: blue
//   /¯¯\\__//¯¯\       3: purple
// 5 \__//¯¯\\__/ 3     4: white
//       \__/           5: grey
//         4            6: green
//
//

#include <ArduinoLog.h>
#include <FastLED.h>
#include <math.h>
#include "utils.h"

const uint8_t LEDS_PER_STRIP = 23;
const uint8_t NUM_STRIPS = 7;
const unsigned short FULL_CIRCLE = 360 * 100;

// Size of the screen, i.e., the length of each side of a square bounding box
// that contains all LEDs in the structure.
const unsigned short SCREEN_SIZE_MM = 520;
const unsigned short SCREEN_HALF_SIZE = SCREEN_SIZE_MM / 2;


class CartesianCoordinates
{
  public:
    short x;
    short y;
};

class PolarCoordinates
{
  public:
    unsigned short radius;
    unsigned short cdegrees;
};

// Represents the geometric information for a single LED
class Led
{
  public:
    uint8_t idx;                             // index in the strip that contains it

    CartesianCoordinates fixedCartesian;  // physical location of the LED relative to the centre. Will not change during execution.
    PolarCoordinates fixedPolar;          // physical location (polar coordinates). Will not change.

    CartesianCoordinates cartesian;       // transformed coordinates. Will change with every FX change.
    PolarCoordinates polar;               // transformed polar coordinates. Will change with every FX change.
};

// Data releated to a single strip.
// Contains the data for all its leds
// and the color buffer for rendering.
class LedStrip
{
  public:

    // Index of the strip in the structure.
    // See map above.
    uint8_t idx;

    // geometry info for each led in the strip
    Led leds[LEDS_PER_STRIP];

    // color buffer for rendering
    CRGB buffer[LEDS_PER_STRIP];

    LedStrip();
};

extern LedStrip STRIPS[NUM_STRIPS];


void initializeGeometry();

// Pick a random rotation angle,
// then apply a rotation matrix to all the LEDs
void applyGlobalRandomRotation();

// Undoes any global coordinate transform by resetting the effective coordinates
// to the original untransformed coordinates
void resetGlobalTransform();

#endif