#pragma once

#include <FastLED.h>
#include <math.h>

#include <vector>

#include "effects-base.h"
#include "effects-utils.h"
#include "energy-param.h"
#include "geometry/geometry.h"
#include "moodlight.h"
#include "utils.h"

// I don't think this will ever show, but why not
class ErrorEffect : public AbstractEffect
{
   public:
    virtual const char* GetName() { return "ErrorEffect"; }

    CRGB evaluate(LedStrip* strip, Led* led, size_t led_idx, milliseconds_t t) override
    {
        if (strip->idx == 0)
            return CRGB::Red;
        else
            return CRGB::Black;
    }
};

class StaticColor : public AbstractEffect
{
   public:
    const char* GetName() override { return "Static Color"; }

    bool wantsLiveColorUpdates() const override { return true; }
    void setColor(CRGB c) override { targetColor = c; }

    CRGB targetColor;   // the color selected by the user, which we will blend towards
    CRGB currentColor;  // the color we are currently displaying, which will blend towards the
                        // targetColor

    void precompute(milliseconds_t t) override
    {
        currentColor = CRGB::blend(currentColor, targetColor, 1);
    }

    CRGB evaluate(LedStrip* strip, Led* led, size_t led_idx, milliseconds_t t) override
    {
        return currentColor;
    }

    StaticColor(CRGB c) : targetColor(c) {}
    StaticColor() : StaticColor(CRGB(255, 255, 170)) {}
};

// Picks a new random effect and randomizes it
AbstractEffect* getRandomEffect();
