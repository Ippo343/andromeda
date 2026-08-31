#pragma once

#include <vector>

#include "effects-base.h"
#include "moodlight.h"

using std::vector;

// Use each individual led strip as an independent moodlight.
// All leds in the same strip have the same color,
// but each strip fluctuates independently
class IndividualStripMoodlight : public AbstractEffect
{
   public:
    virtual const char* GetName() { return "Individual Strip Moodlight"; }

    vector<MoodLight> moodlights;
    vector<CRGB> colors;

    IndividualStripMoodlight()
        : moodlights(GEOMETRY.getNumStrips()), colors(GEOMETRY.getNumStrips())
    {
    }

    void precompute(milliseconds_t t) override
    {
        FOR_EACH_STRIP { colors[iStrip] = moodlights[iStrip].evaluate(); }
    }

    CRGB evaluate(LedStrip* strip, Led* led, size_t led_idx, milliseconds_t t) override
    {
        return colors[strip->idx];
    }
};
