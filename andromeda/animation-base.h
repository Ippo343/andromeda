#ifndef ANIMATION_BASE_H
#define ANIMATION_BASE_H

#include "utils.h"
#include "geometry.h"

// Abstract base class for all animations.
// Animations are not really effects, they are meant to be small programs
// that take direct control of all the strips and run once.
// The control flow will not loop them, it will use them as transitions
// between different effects.
class AbstractAnimation
{
  public:
    virtual void run();

    // Reset all buffers to black and brightness to max,
    // to prevent funny inputs going into the next effect
    virtual void cleanup()
    {
      FOR_EACH_STRIP {
        FOR_EACH_LED {
          STRIPS[iStrip].buffer[iLed] = CRGB::Black;
        }
      }

      FastLED.setBrightness(255);
    }
};

#endif