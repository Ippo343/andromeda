#ifndef EFFECTS_H
#define EFFECTS_H

#include <vector>
#include <FastLED.h>

#include "geometry.h"
#include "utils.h"
#include "effects-base.h"
#include "effects-utils.h"
#include "moodlight.h"

// I don't think this will ever show, but why not
class ErrorEffect : public AbstractEffect
{
  public:
    virtual const char* GetName()
    {
      return "ErrorEffect";
    }

    CRGB evaluate(LedStrip* strip, Led* led, milliseconds t) override
    {
      if (strip->idx == 0)
        return CRGB::Red;
      else
        return CRGB::Black;
    }
};

// Picks a new random effect and randomizes it
AbstractEffect* getRandomEffect();

#endif