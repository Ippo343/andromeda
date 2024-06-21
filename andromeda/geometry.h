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

#include <FastLED.h>
#include "utils.h"

const byte LEDS_PER_STRIP = 23;
const byte NUM_STRIPS = 7;

struct coords
{
  short x;
  short y;
};

// Represents the geometric information for a single LED
// For the moment it only stores its index in the strip,
// but in the future it will be extended to include its coordinates.
struct Led
{
  public:
    byte idx;
    coords cartesian;
};


// Data releated to a single strip.
// Contains the data for all its leds
// and the color buffer for rendering.
class LedStrip
{
  public:

    // Index of the strip in the structure.
    // See map above.
    byte idx;

    // geometry info for each led in the strip
    Led leds[LEDS_PER_STRIP];

    // color buffer for rendering
    CRGB buffer[LEDS_PER_STRIP];

    LedStrip() {
      // Initialize each led with its index in the strip
      FOR_EACH_LED {
        this->leds[iLed].idx = iLed;
        this->buffer[iLed] = CRGB::Black;
      }
    }
};


LedStrip STRIPS[NUM_STRIPS];


// Coordinates of each LED, in millimeters, relative to the center of the structure.
// See the python helper in led-coordinates/ to see where these come from
const PROGMEM coords relative_led_coordinates[NUM_STRIPS][LEDS_PER_STRIP] = {
  { {   -7,   65 }, {   11,   64 }, {   27,   59 }, {   42,   49 }, {   54,   37 }, {   61,   21 }, {   65,    4 }, {   63,  -13 }, {   58,  -30 }, {   48,  -44 }, {   34,  -55 }, {   18,  -62 }, {    1,  -65 }, {  -16,  -63 }, {  -32,  -56 }, {  -46,  -46 }, {  -57,  -32 }, {  -63,  -16 }, {  -65,    2 }, {  -62,   19 }, {  -55,   35 }, {  -44,   48 }, {  -30,   58 } },
  { {   14,  135 }, {   -2,  133 }, {  -19,  136 }, {  -34,  143 }, {  -47,  154 }, {  -56,  168 }, {  -62,  184 }, {  -63,  201 }, {  -59,  217 }, {  -52,  232 }, {  -40,  244 }, {  -26,  253 }, {  -10,  258 }, {    7,  259 }, {   23,  255 }, {   38,  247 }, {   50,  235 }, {   58,  220 }, {   63,  204 }, {   62,  187 }, {   58,  171 }, {   49,  157 }, {   37,  145 } },
  { {  121,   55 }, {  112,   70 }, {  107,   86 }, {  106,  102 }, {  109,  119 }, {  117,  134 }, {  129,  146 }, {  143,  155 }, {  159,  160 }, {  176,  161 }, {  193,  156 }, {  207,  148 }, {  219,  136 }, {  228,  122 }, {  232,  105 }, {  232,   88 }, {  227,   72 }, {  219,   58 }, {  206,   46 }, {  191,   38 }, {  175,   34 }, {  158,   35 }, {  142,   40 } },
  { {  149,  -38 }, {  165,  -35 }, {  182,  -36 }, {  197,  -41 }, {  211,  -51 }, {  222,  -64 }, {  229,  -79 }, {  232,  -95 }, {  230, -112 }, {  224, -128 }, {  214, -141 }, {  201, -151 }, {  186, -158 }, {  169, -160 }, {  152, -158 }, {  137, -151 }, {  124, -141 }, {  114, -127 }, {  108, -112 }, {  106,  -95 }, {  109,  -79 }, {  116,  -63 }, {  127,  -51 } },
  { {   51, -158 }, {   59, -173 }, {   63, -189 }, {   62, -206 }, {   58, -222 }, {   49, -237 }, {   36, -248 }, {   21, -256 }, {    5, -259 }, {  -12, -258 }, {  -28, -253 }, {  -42, -243 }, {  -53, -231 }, {  -60, -215 }, {  -63, -199 }, {  -62, -182 }, {  -56, -166 }, {  -46, -153 }, {  -33, -142 }, {  -18, -135 }, {   -1, -133 }, {   16, -135 }, {   31, -141 } },
  { { -108, -105 }, { -113, -122 }, { -122, -137 }, { -135, -148 }, { -151, -156 }, { -168, -159 }, { -185, -156 }, { -201, -150 }, { -214, -138 }, { -224, -124 }, { -229, -108 }, { -230,  -90 }, { -225,  -73 }, { -216,  -58 }, { -204,  -47 }, { -188,  -39 }, { -171,  -36 }, { -154,  -38 }, { -137,  -45 }, { -124,  -56 }, { -114,  -70 }, { -109,  -87 }, { -108, -104 } },
  { { -154,   37 }, { -171,   35 }, { -187,   38 }, { -202,   45 }, { -215,   55 }, { -224,   69 }, { -230,   84 }, { -231,  101 }, { -228,  117 }, { -221,  132 }, { -210,  145 }, { -196,  154 }, { -180,  159 }, { -163,  159 }, { -147,  156 }, { -132,  148 }, { -120,  136 }, { -112,  122 }, { -107,  106 }, { -107,   89 }, { -112,   73 }, { -120,   59 }, { -132,   47 } },
};

// Size of the screen, i.e., the length of each side of a square bounding box
// that contains all LEDs in the structure.
const unsigned short SCREEN_SIZE_MM = 520;
const unsigned short SCREEN_HALF_SIZE = SCREEN_SIZE_MM / 2;


void initializeGeometry() {

  FOR_EACH_STRIP {
    STRIPS[iStrip].idx = iStrip;
  }

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

  FOR_EACH_STRIP {
    FOR_EACH_LED {
      STRIPS[iStrip].leds[iLed].cartesian = relative_led_coordinates[iStrip][iLed];
    }
  }

  Serial.println("Initialized geometry");
}

#endif
