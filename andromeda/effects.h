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
    CRGB evaluate(LedStrip strip, Led led, milliseconds t) override
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

    milliseconds step = 30;
    byte idxOn;

    CRGB color[NUM_STRIPS];

    virtual void precompute(milliseconds t) override
    {
      FOR_EACH_STRIP {
        color[iStrip] = moodlights[iStrip].evaluate(0, t);
      }

      // Pick an LED to turn on, rotating along the strip
      idxOn = (int)(t / step) % (LEDS_PER_STRIP);
    }

    CRGB evaluate(LedStrip strip, Led led, milliseconds t) override
    {
      // If this is the led to turn on, apply the color;
      // otherwise, leave it unchanged.
      // They will be faded away in postprocessing
      return led.idx == idxOn
        ? color[strip.idx]
        : strip.buffer[led.idx];
    }

    virtual void postprocess(milliseconds t) override
    {
      FOR_EACH_STRIP {
        fadeToBlackBy(STRIPS[iStrip].buffer, LEDS_PER_STRIP, 25);
      }
    }
};

#endif