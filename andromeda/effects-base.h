#ifndef EFFECTS_BASE_H
#define EFFECTS_BASE_H

// This header defines the base classes that all effects are built upon

#include "utils.h"
#include "geometry.h"

// Abstract base class for all effects.
// Each effect must define a way to compute the color for a specific led at time t.
// The info about the strip that contains the led is also available, as in the future
// strips will also hold their own geometry and an effect might use it.
// Optionally an effect can also define a randomize method to select random parameters.
class AbstractEffect
{
  public:

    virtual void precompute(milliseconds t) { return ; }

    virtual CRGB evaluate(LedStrip strip, Led led, milliseconds t);

    virtual void randomize() { return; }

    // Computes the function over all the strips
    void render(LedStrip* strips, milliseconds t)
    {
      FOR_EACH_STRIP {
        FOR_EACH_LED {
          strips[iStrip].buffer[iLed] = this->evaluate(strips[iStrip], strips[iStrip].leds[iLed], t);
        }
      }
    }
};

#endif