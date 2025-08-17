#ifndef EFFECTS_BASE_H
#define EFFECTS_BASE_H

// This header defines the base classes that all effects are built upon

#include "utils.h"
#include "geometry.h"
#include "control-hints.h"

// Abstract base class for all effects.
// Each effect must define a way to compute the color for a specific led at time t.
// The info about the strip that contains the led is also available, as in the future
// strips will also hold their own geometry and an effect might use it.
// Optionally an effect can also define a randomize method to select random parameters.
class AbstractEffect
{
  public:
    virtual const char* GetName();

    control_hints_t controlHints = ControlHints::NONE;

    virtual ~AbstractEffect() { }

    // Called at the beginning of the frame.
    // Allows the effect to cache values that will be reused during evaluate
    virtual void precompute(milliseconds_t t) { return ; }

    // Evaluates the effect on each led
    virtual CRGB evaluate(LedStrip* strip, Led* led, milliseconds_t t);

    // Called when all the led's have been evaluated
    // to allow postprocess effects like blurring and fading
    virtual void postprocess(milliseconds_t t) { return ; }

    // Randomize the effect's parameters
    virtual void randomize() { return; }

    // Computes the function over all the strips
    void render(LedStrip* strips, milliseconds_t t)
    {
      FOR_EACH_STRIP {
        FOR_EACH_LED {
          strips[iStrip].buffer[iLed] = this->evaluate(&strips[iStrip], &strips[iStrip].leds[iLed], t);
        }
      }
    }
};

#endif