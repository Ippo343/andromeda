#ifndef EFFECTS_BASE_H
#define EFFECTS_BASE_H

// This header defines the base classes that all effects are built upon

#include "geometry.h"

// Abstract base class for all effects.
// Each effect must define a way to compute the color for a specific led at time t.
// The info about the strip that contains the led is also available, as in the future
// strips will also hold their own geometry and an effect might use it.
// Optionally an effect can also define a randomize method to select random parameters.
class AbstractEffect
{
  public:

    // TODO: this needs a method to precompute values at the start of each refresh

    virtual CRGB evaluate(LedStrip strip, Led led, float t);

    virtual void randomize() { return; }

    // Computes the function over all the strips
    void render(LedStrip* strips, float t)
    {
      FOR_EACH_STRIP {
        FOR_EACH_LED {
          strips[iStrip].buffer[iLed] = this->evaluate(strips[iStrip], strips[iStrip].leds[iLed], t);
        }
      }
    }
};

#endif