#pragma once

#include "effects-base.h"
#include "moodlight.h"
#include "utils.h"

// Rotating beams of light
class NinjaStar : public AbstractEffect
{
   public:
    virtual const char* GetName() { return "Ninja Star"; }

    RandParam<milliseconds_t, 5000, 5000> duration;
    RandParam<unsigned short, 6, 6> beams;
    RandSign flip;

    MoodLight inner;
    MoodLight outer;
    CRGB innerColor;
    CRGB outerColor;
    uint8_t offset;

    // scale8(v, v) applied 4x is a pure function of v (0-255) with no
    // per-instance parameters, so it's memoized once across all instances
    // instead of recomputed per LED per frame.
    static inline uint8_t pow16LUT[256] = {};
    static inline bool pow16LUTReady = false;

    static uint8_t pow16(uint8_t v)
    {
        if (!pow16LUTReady)
        {
            for (int i = 0; i < 256; i++)
            {
                uint8_t r = (uint8_t)i;
                for (size_t j = 0; j < 4; j++) r = scale8(r, r);
                pow16LUT[i] = r;
            }
            pow16LUTReady = true;
        }
        return pow16LUT[v];
    }

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

        uint8_t v = pow16(sin8(theta + offset));

        unsigned short scaledRadius = map(led->polar.radius, 0, GEOMETRY.getScreenRadius(), 0, 255);
        CRGB color = blend(innerColor, outerColor, scaledRadius);

        return color % v;
    }
};
