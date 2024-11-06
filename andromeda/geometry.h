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
#include <math.h>
#include "utils.h"

const byte LEDS_PER_STRIP = 23;
const byte NUM_STRIPS = 7;
const unsigned short FULL_CIRCLE = 360 * 100;


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
    byte idx;                             // index in the strip that contains it

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
const PROGMEM CartesianCoordinates cartesian_led_coordinates[NUM_STRIPS][LEDS_PER_STRIP] = {
  { {   -7,   65 }, {   11,   64 }, {   27,   59 }, {   42,   49 }, {   54,   37 }, {   61,   21 }, {   65,    4 }, {   63,  -13 }, {   58,  -30 }, {   48,  -44 }, {   34,  -55 }, {   18,  -62 }, {    1,  -65 }, {  -16,  -63 }, {  -32,  -56 }, {  -46,  -46 }, {  -57,  -32 }, {  -63,  -16 }, {  -65,    2 }, {  -62,   19 }, {  -55,   35 }, {  -44,   48 }, {  -30,   58 } },
  { {   14,  135 }, {   -2,  133 }, {  -19,  136 }, {  -34,  143 }, {  -47,  154 }, {  -56,  168 }, {  -62,  184 }, {  -63,  201 }, {  -59,  217 }, {  -52,  232 }, {  -40,  244 }, {  -26,  253 }, {  -10,  258 }, {    7,  259 }, {   23,  255 }, {   38,  247 }, {   50,  235 }, {   58,  220 }, {   63,  204 }, {   62,  187 }, {   58,  171 }, {   49,  157 }, {   37,  145 } },
  { {  121,   55 }, {  112,   70 }, {  107,   86 }, {  106,  102 }, {  109,  119 }, {  117,  134 }, {  129,  146 }, {  143,  155 }, {  159,  160 }, {  176,  161 }, {  193,  156 }, {  207,  148 }, {  219,  136 }, {  228,  122 }, {  232,  105 }, {  232,   88 }, {  227,   72 }, {  219,   58 }, {  206,   46 }, {  191,   38 }, {  175,   34 }, {  158,   35 }, {  142,   40 } },
  { {  149,  -38 }, {  165,  -35 }, {  182,  -36 }, {  197,  -41 }, {  211,  -51 }, {  222,  -64 }, {  229,  -79 }, {  232,  -95 }, {  230, -112 }, {  224, -128 }, {  214, -141 }, {  201, -151 }, {  186, -158 }, {  169, -160 }, {  152, -158 }, {  137, -151 }, {  124, -141 }, {  114, -127 }, {  108, -112 }, {  106,  -95 }, {  109,  -79 }, {  116,  -63 }, {  127,  -51 } },
  { {   51, -158 }, {   59, -173 }, {   63, -189 }, {   62, -206 }, {   58, -222 }, {   49, -237 }, {   36, -248 }, {   21, -256 }, {    5, -259 }, {  -12, -258 }, {  -28, -253 }, {  -42, -243 }, {  -53, -231 }, {  -60, -215 }, {  -63, -199 }, {  -62, -182 }, {  -56, -166 }, {  -46, -153 }, {  -33, -142 }, {  -18, -135 }, {   -1, -133 }, {   16, -135 }, {   31, -141 } },
  { { -108, -105 }, { -113, -122 }, { -122, -137 }, { -135, -148 }, { -151, -156 }, { -168, -159 }, { -185, -156 }, { -201, -150 }, { -214, -138 }, { -224, -124 }, { -229, -108 }, { -230,  -90 }, { -225,  -73 }, { -216,  -58 }, { -204,  -47 }, { -188,  -39 }, { -171,  -36 }, { -154,  -38 }, { -137,  -45 }, { -124,  -56 }, { -114,  -70 }, { -109,  -87 }, { -108, -104 } },
  { { -154,   37 }, { -171,   35 }, { -187,   38 }, { -202,   45 }, { -215,   55 }, { -224,   69 }, { -230,   84 }, { -231,  101 }, { -228,  117 }, { -221,  132 }, { -210,  145 }, { -196,  154 }, { -180,  159 }, { -163,  159 }, { -147,  156 }, { -132,  148 }, { -120,  136 }, { -112,  122 }, { -107,  106 }, { -107,   89 }, { -112,   73 }, { -120,   59 }, { -132,   47 } },
};

// Theta is in CENTI-DEGREES, because it makes integer math easier when writing effects
const PROGMEM PolarCoordinates polar_led_coordinates[NUM_STRIPS][LEDS_PER_STRIP] = {
  { {   65,  9615 }, {   65,  8025 }, {   65,  6541 }, {   65,  4940 }, {   65,  3442 }, {   65,  1900 }, {   65,   352 }, {   64, 34834 }, {   65, 33265 }, {   65, 31749 }, {   65, 30172 }, {   65, 28619 }, {   65, 27088 }, {   65, 25575 }, {   64, 24026 }, {   65, 22500 }, {   65, 20931 }, {   65, 19425 }, {   65, 17824 }, {   65, 16296 }, {   65, 14753 }, {   65, 13251 }, {   65, 11735 } },
  { {  136,  8408 }, {  133,  9086 }, {  137,  9795 }, {  147, 10337 }, {  161, 10697 }, {  177, 10843 }, {  194, 10862 }, {  211, 10740 }, {  225, 10521 }, {  238, 10263 }, {  247,  9931 }, {  254,  9587 }, {  258,  9222 }, {  259,  8845 }, {  256,  8485 }, {  250,  8125 }, {  240,  7799 }, {  228,  7523 }, {  214,  7284 }, {  197,  7166 }, {  181,  7126 }, {  164,  7267 }, {  150,  7569 } },
  { {  133,  2444 }, {  132,  3201 }, {  137,  3879 }, {  147,  4390 }, {  161,  4751 }, {  178,  4887 }, {  195,  4854 }, {  211,  4731 }, {  226,  4518 }, {  239,  4245 }, {  248,  3895 }, {  254,  3556 }, {  258,  3184 }, {  259,  2815 }, {  255,  2435 }, {  248,  2077 }, {  238,  1760 }, {  227,  1483 }, {  211,  1259 }, {  195,  1125 }, {  178,  1099 }, {  162,  1249 }, {  148,  1573 } },
  { {  154, 34569 }, {  169, 34802 }, {  186, 34881 }, {  201, 34824 }, {  217, 34641 }, {  231, 34392 }, {  242, 34097 }, {  251, 33773 }, {  256, 33404 }, {  258, 33026 }, {  256, 32662 }, {  251, 32308 }, {  244, 31965 }, {  233, 31657 }, {  219, 31389 }, {  204, 31222 }, {  188, 31133 }, {  171, 31191 }, {  156, 31396 }, {  142, 31813 }, {  135, 32407 }, {  132, 33149 }, {  137, 33812 } },
  { {  166, 28789 }, {  183, 28883 }, {  199, 28843 }, {  215, 28675 }, {  229, 28464 }, {  242, 28168 }, {  251, 27826 }, {  257, 27469 }, {  259, 27111 }, {  258, 26734 }, {  255, 26368 }, {  247, 26019 }, {  237, 25708 }, {  223, 25441 }, {  209, 25243 }, {  192, 25119 }, {  175, 25136 }, {  160, 25327 }, {  146, 25692 }, {  136, 26241 }, {  133, 26957 }, {  136, 27676 }, {  144, 28240 } },
  { {  151, 22419 }, {  166, 22719 }, {  183, 22831 }, {  200, 22763 }, {  217, 22593 }, {  231, 22342 }, {  242, 22014 }, {  251, 21673 }, {  255, 21282 }, {  256, 20897 }, {  253, 20525 }, {  247, 20137 }, {  237, 19798 }, {  224, 19503 }, {  209, 19297 }, {  192, 19172 }, {  175, 19189 }, {  159, 19386 }, {  144, 19818 }, {  136, 20430 }, {  134, 21155 }, {  139, 21860 }, {  150, 22392 } },
  { {  158, 16649 }, {  175, 16843 }, {  191, 16851 }, {  207, 16744 }, {  222, 16565 }, {  234, 16288 }, {  245, 15994 }, {  252, 15638 }, {  256, 15284 }, {  257, 14915 }, {  255, 14538 }, {  249, 14184 }, {  240, 13854 }, {  228, 13571 }, {  214, 13330 }, {  198, 13173 }, {  181, 13142 }, {  166, 13255 }, {  151, 13527 }, {  139, 14025 }, {  134, 14690 }, {  134, 15382 }, {  140, 16040 } },
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
      STRIPS[iStrip].leds[iLed].fixedCartesian = cartesian_led_coordinates[iStrip][iLed];
      STRIPS[iStrip].leds[iLed].fixedPolar     = polar_led_coordinates[iStrip][iLed];

      STRIPS[iStrip].leds[iLed].cartesian = cartesian_led_coordinates[iStrip][iLed];
      STRIPS[iStrip].leds[iLed].polar     = polar_led_coordinates[iStrip][iLed];
    }
  }

  Log.noticeln("Initialized geometry");
}


// Pick a random rotation angle,
// then apply a rotation matrix to all the LEDs
void applyGlobalRandomRotation()
{
  // Pick a random angle for the rotation
  float theta = (random(0, 1000) / 1000.0) * 2 * PI;

  // Same angle, in centidegrees (to transform the polar coordinates)
  int tcDeg = (int)(theta * (100 * 180.0 / PI));

  Log.noticeln("Applying global rotation: %d", (tcDeg / 100));

  float cosT = cos(theta);
  float sinT = sin(theta);

  FOR_EACH_STRIP {
    FOR_EACH_LED {

      // Real physical coordinates of the LED
      CartesianCoordinates r = STRIPS[iStrip].leds[iLed].fixedCartesian;

      // This is actually the INVERSE rotation matrix.
      // Picture the Swipe animation doing a horizontal swipe of the LEDs.
      // Picture picking a 90° rotation counterclockwise: the swipe must now be vertical.
      // This means the swipe will be drawn moving towards the topmost LED.
      // To do that, the topmost LED must "coincide" with the rightmost position on the structure,
      // which means we have to apply the inverse rotation to the temporary coordinates.
      // It's poorly explained but it makes sense in my head.

      STRIPS[iStrip].leds[iLed].cartesian.x = (short)(  r.x * cosT + r.y * sinT);
      STRIPS[iStrip].leds[iLed].cartesian.y = (short)(- r.x * sinT + r.y * cosT);

      STRIPS[iStrip].leds[iLed].polar.cdegrees = ((int)STRIPS[iStrip].leds[iLed].polar.cdegrees - tcDeg) % FULL_CIRCLE;
    }
  }
}

// Undoes any global coordinate transform by resetting the effective coordinates
// to the original untransformed coordinates
void resetGlobalTransform()
{
  Log.noticeln("Resetting global transform");
  FOR_EACH_STRIP {
    FOR_EACH_LED {
      STRIPS[iStrip].leds[iLed].cartesian = STRIPS[iStrip].leds[iLed].fixedCartesian;
      STRIPS[iStrip].leds[iLed].polar     = STRIPS[iStrip].leds[iLed].fixedPolar;
    }
  }
}

#endif
