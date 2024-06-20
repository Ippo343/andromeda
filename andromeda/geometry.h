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

// Represents the geometric information for a single LED
// For the moment it only stores its index in the strip,
// but in the future it will be extended to include its coordinates.
struct Led
{
  public:
    byte idx;
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

  // TODO: actually initialize geometry (i.e. compute each led's coordinates)

  Serial.println("Initialized geometry");
}

#endif
