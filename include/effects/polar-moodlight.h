#pragma once

#include "effects-base.h"
#include "utils.h"

// Just a simple moodlight, but with three waves radiating to/from the center
class PolarMoodlight : public AbstractEffect
{
   public:
    virtual const char* GetName() { return "PolarMoodlight"; }

    // minBpm, maxBpm, minScale, maxScale
    RandSine<1, 15> red;
    RandSine<1, 15> green;
    RandSine<1, 15> blue;

    CRGB evaluate(LedStrip* strip, Led* led, size_t led_idx, milliseconds_t t) override
    {
        uint8_t R = red.evaluate(led->polar.radius);
        uint8_t G = green.evaluate(led->polar.radius);
        uint8_t B = blue.evaluate(led->polar.radius);

        return CRGB(R, G, B);
    }
};
