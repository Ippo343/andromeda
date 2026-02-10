#pragma once

// This header defines the base classes that all effects are built upon

#include "control-hints.h"
#include "geometry/geometry.h"
#include "utils.h"

// Abstract base class for all effects.
// Each effect must define a way to compute the color for a specific led at time t.
// The info about the strip that contains the led is also available, as in the future
// strips will also hold their own geometry and an effect might use it.
// Optionally an effect can also define a randomize method to select random parameters.
class AbstractEffect
{
   public:
    virtual const char* GetName() = 0;

    control_hints_t controlHints = ControlHints::NONE;

    virtual ~AbstractEffect() {}

    // Called at the beginning of the frame.
    // Allows the effect to cache values that will be reused during evaluate
    virtual void precompute(milliseconds_t t) { return; }

    // Evaluates the effect on each led
    virtual CRGB evaluate(LedStrip* strip, Led* led, milliseconds_t t) = 0;

    // Called when all the led's have been evaluated
    // to allow postprocess effects like blurring and fading
    virtual void postprocess(milliseconds_t t) { return; }

    // Computes the function over all the strips
    void render(milliseconds_t t)
    {
        FOR_EACH_STRIP
        {
            FOR_EACH_LED(iStrip)
            {
                GEOMETRY.getStrip(iStrip).buffer[iLed] = this->evaluate(
                    &GEOMETRY.getStrip(iStrip), &GEOMETRY.getStrip(iStrip).leds[iLed], t);
            }
        }
    }
};
