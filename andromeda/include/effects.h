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

    CRGB evaluate(LedStrip* strip, Led* led, milliseconds_t t) override
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

    CRGB color;
    CRGB evaluate(LedStrip* strip, Led* led, milliseconds_t t) override { return color; }

    StaticColor(CRGB c) : color(c) {}
    StaticColor() : StaticColor(CRGB(255, 255, 170)) {}
};

// Picks a new random effect and randomizes it
AbstractEffect* getRandomEffect();
