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

    CRGB colors[NUM_STRIPS];

    void precompute(milliseconds t) override
    {
      FOR_EACH_STRIP {
        colors[iStrip] = moodlights[iStrip].evaluate(0, t);
      }
    }

    CRGB evaluate(LedStrip strip, Led led, milliseconds t) override
    {
      return colors[strip.idx];
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


// I don't think this will ever show, but why not
class ErrorEffect : public AbstractEffect
{
  public:
    CRGB evaluate(LedStrip strip, Led led, milliseconds t) override
    {
      if (strip.idx == 0)
        return CRGB::Red;
      else
        return CRGB::Black;
    }
};


AbstractEffect* getRandomEffect() {

  // You cannot have an array of types on this thing.
  // I have a prototype where I had an array of template functions
  // that would instantiate each effect, but unsurprisingly it crashes.
  // For the moment, KISS will do.
  // TODO: array of template functions because I can.

  byte EFFECTS_COUNT = 2;
  byte selection = analogRead(0) % EFFECTS_COUNT;

  switch (selection)
  {
    case 0:
      return new IndividualStripMoodlight();
    case 1:
      return new LoopingPoint();
    default:
      return new ErrorEffect();
  }
}

#endif