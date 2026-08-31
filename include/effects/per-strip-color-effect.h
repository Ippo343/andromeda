#pragma once

#include <vector>

#include "effects-base.h"

using std::vector;

// Shared base for effects whose entire per-frame output is "pick one color per strip
// in precompute()" - IndividualStripMoodlight, SaturationGlow, and IndividualStripDrift
// all reduce to this shape at render time, whether or not they need extra per-strip
// timing/interpolation state (transition start/end times, previous colors, ...) to
// arrive at that color. Centralizing render() also lets it use fill_solid() instead of
// a virtual evaluate() call per LED.
class PerStripColorEffect : public AbstractEffect
{
   public:
    vector<CRGB> colors;

    PerStripColorEffect() : colors(GEOMETRY.getNumStrips()) {}

    CRGB evaluate(LedStrip* strip, Led* led, size_t led_idx, milliseconds_t t) override
    {
        return colors[strip->idx];
    }

    void render(milliseconds_t t) override
    {
        FOR_EACH_STRIP
        {
            LedStrip& strip = GEOMETRY.getStrip(iStrip);
            fill_solid(strip.buffer, strip.num_leds, colors[iStrip]);
        }
    }
};
