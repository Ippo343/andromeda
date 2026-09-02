#pragma once

#include <vector>

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
        ensurePerLedCache();
    }

    CRGB evaluate(LedStrip* strip, Led* led, size_t led_idx, milliseconds_t t) override
    {
        uint8_t theta = thetaCache[strip->idx][led_idx];
        uint8_t v = pow16(sin8(theta + offset));

        uint8_t scaledRadius = radiusCache[strip->idx][led_idx];
        CRGB color = blend(innerColor, outerColor, scaledRadius);

        return color % v;
    }

    // Public so tests can build the per-LED cache without going through the
    // full precompute() (which also samples inner/outer MoodLight colors and
    // derives offset from real t - more than some tests want to control).
    // theta and scaledRadius are pure functions of a LED's fixed polar
    // coordinates (and beams, itself fixed for the effect's lifetime) - no
    // time dependency, so they're computed once instead of remapped every
    // LED every frame. Built lazily on the first precompute() rather than
    // in the constructor, since a ROTATE_SPACE effect's coordinates aren't
    // final until MissionControl::finishTransition() applies the rotation,
    // which happens after construction but before the first frame.
    std::vector<std::vector<uint8_t>> thetaCache;
    std::vector<std::vector<uint8_t>> radiusCache;
    bool perLedCacheReady = false;

    void ensurePerLedCache()
    {
        if (perLedCacheReady) return;

        thetaCache.resize(GEOMETRY.getNumStrips());
        radiusCache.resize(GEOMETRY.getNumStrips());

        FOR_EACH_STRIP
        {
            LedStrip& strip = GEOMETRY.getStrip(iStrip);
            thetaCache[iStrip].resize(strip.num_leds);
            radiusCache[iStrip].resize(strip.num_leds);

            FOR_EACH_LED(iStrip)
            {
                Led& led = strip.leds[iLed];
                thetaCache[iStrip][iLed] =
                    map((led.polar.cdegrees * beams) % FULL_CIRCLE, 0, FULL_CIRCLE, 0, 255);
                // getMaxLedRadius(), not getScreenRadius(): the latter only
                // reaches a side midpoint, so map() extrapolated past 255 for
                // every LED beyond it and the uint8_t store wrapped mod 256.
                radiusCache[iStrip][iLed] =
                    map(led.polar.radius, 0, GEOMETRY.getMaxLedRadius(), 0, 255);
            }
        }

        perLedCacheReady = true;
    }
};
