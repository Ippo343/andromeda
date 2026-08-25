#pragma once

#include "effects-base.h"
#include "moodlight.h"
#include "utils.h"

// Rotating beams of light
class NinjaStar : public AbstractEffect
{
   public:
    virtual const char* GetName() { return "NinjaStar"; }

    RandParam<milliseconds_t, 5000, 5000> duration;
    RandParam<unsigned short, 6, 6> beams;
    RandSign flip;

    MoodLight inner;
    MoodLight outer;
    CRGB innerColor;
    CRGB outerColor;
    uint8_t offset;

    void precompute(milliseconds_t t) override
    {
        innerColor = inner.evaluate();
        outerColor = outer.evaluate();
        offset = map((flip * t) % duration, 0, duration, 0, 255);
    }

    CRGB evaluate(LedStrip* strip, Led* led, size_t led_idx, milliseconds_t t) override
    {
        // TODO: precompute the mapping of the LEDs during the constructor
        uint8_t theta = map((led->polar.cdegrees * beams) % FULL_CIRCLE, 0, FULL_CIRCLE, 0, 255);

        // TODO: this is a great opportunity for a LUT
        uint8_t v = sin8(theta + offset);
        for (size_t i = 0; i < 4; i++) v = scale8(v, v);

        unsigned short scaledRadius = map(led->polar.radius, 0, GEOMETRY.getScreenRadius(), 0, 255);
        CRGB color = blend(innerColor, outerColor, scaledRadius);

        return color % v;
    }
};
