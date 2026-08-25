#pragma once

#include "effects-base.h"

// I don't think this will ever show, but why not
class ErrorEffect : public AbstractEffect
{
   public:
    virtual const char* GetName() { return "ErrorEffect"; }

    CRGB evaluate(LedStrip* strip, Led* led, size_t led_idx, milliseconds_t t) override
    {
        if (strip->idx == 0)
            return CRGB::Red;
        else
            return CRGB::Black;
    }
};
