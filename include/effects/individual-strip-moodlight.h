#pragma once

#include <vector>

#include "effects/per-strip-color-effect.h"
#include "moodlight.h"

using std::vector;

// Use each individual led strip as an independent moodlight.
// All leds in the same strip have the same color,
// but each strip fluctuates independently
class IndividualStripMoodlight : public PerStripColorEffect
{
   public:
    virtual const char* GetName() { return "Individual Strip Moodlight"; }

    vector<MoodLight> moodlights;

    IndividualStripMoodlight() : moodlights(GEOMETRY.getNumStrips()) {}

    void precompute(milliseconds_t t) override
    {
        FOR_EACH_STRIP { colors[iStrip] = moodlights[iStrip].evaluate(); }
    }
};
