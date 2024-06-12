#ifndef EFFECTS_H
#define EFFECTS_H

#include <FastLED.h>
#include "geometry.h"
#include "utils.h"
#include "effects-base.h"
#include "effects-utils.h"

// Most of the effects I have written or plan to write
// use one or more moodlights. Not great to have them here,
// but it will do for the time being.
// TODO: In the future any effect that wants a moodlight should have it as a member.
MoodLight moodlights[NUM_STRIPS];


// Use each individual led strip as an independent moodlight.
// All leds in the same strip have the same color,
// but each strip fluctuates independently
class IndividualStripMoodlight : public AbstractEffect
{
  public:
    CRGB evaluate(LedStrip strip, Led led, float t) override
    {
      // ignore the led index to force each led to the same color
      // TODO: this is horribly inefficient.
      // There should be a base class for all effects that apply to the full strip
      // to avoid recomputing the same thing 23 times
      return moodlights[strip.idx].evaluate(0, t);
    }
};


// One single led turned on per strip,
// looping around it very fast
class LoopingPoint : public AbstractEffect
{
  public:
    CRGB evaluate(LedStrip strip, Led led, float t) override
    {
      CRGB color = moodlights[0].evaluate(0, t);
      
      // Index of the only led that should be on,
      // all the others will be black
      // TODO: this 100.0 is a parameter that should be either randomized or set more intelligently
      // TODO: this index should be precomputed before rendering each loop
      int idxOn = (int)(t / 100.0) % (LEDS_PER_STRIP);

      return led.idx == idxOn ? color : CRGB::Black;
    }
};

#endif