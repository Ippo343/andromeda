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
    // The led index is not stored in the led anymore, instead the loop counter is passed as an
    // argument This is because removing the index from the struct allows us to align to cache lines
    virtual CRGB evaluate(LedStrip* strip, Led* led, size_t led_idx, milliseconds_t t) = 0;

    // Called when all the led's have been evaluated
    // to allow postprocess effects like blurring and fading
    virtual void postprocess(milliseconds_t t) { return; }

    // Whether this effect accepts a live color pushed in from the web UI while
    // it is running (bypassing the normal transition/rotation flow). Most
    // effects don't. StaticColor opts in because a web COLOR command must take
    // effect immediately, without waiting for a transition.
    //
    // Breadcrumb for later: today an effect that opts in treats the color as
    // its entire output (see StaticColor::setColor). If we ever want an effect
    // to instead use an externally-pushed color as a palette/accent reference
    // alongside its own generated pattern, this is the seam to extend -
    // likely by giving setColor's caller more context than just CRGB.
    virtual bool wantsLiveColorUpdates() const { return false; }
    virtual void setColor(CRGB c) {}

    // Computes the function over all the strips
    void render(milliseconds_t t)
    {
        FOR_EACH_STRIP
        {
            LedStrip& strip = GEOMETRY.getStrip(iStrip);
            FOR_EACH_LED(iStrip)
            {
                strip.buffer[iLed] = this->evaluate(&strip, &strip.leds[iLed], iLed, t);
            }
        }
    }
};
