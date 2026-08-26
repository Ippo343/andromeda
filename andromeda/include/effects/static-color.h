#pragma once

#include "effects-base.h"

class StaticColor : public AbstractEffect
{
   public:
    const char* GetName() override { return "Static Color"; }

    bool wantsLiveColorUpdates() const override { return true; }
    void setColor(CRGB c) override { targetColor = c; }

    CRGB targetColor;   // the color selected by the user, which we will blend towards
    CRGB currentColor;  // the color we are currently displaying, which will blend towards the
                        // targetColor

    // Frame-rate independent blend towards targetColor: fraction of the remaining distance
    // closed per second, converted each frame into a CRGB::blend() amount via dt - see
    // accumulateFadeAmount(). Chosen to match the old fixed blend(cur, target, 1) step at a
    // ~60fps reference.
    constexpr static float BLEND_RATE_PER_SECOND = 0.235f;
    float blendDebt = 0;

    // Elapsed time (dt) since the previous frame, same lastT/hasLastT idiom as
    // ElectricSparks (see include/effects/electric-sparks.h) - `t` is always an absolute
    // millis() value, never a delta, so effects that need dt must track it themselves.
    milliseconds_t lastT = 0;
    bool hasLastT = false;

    void precompute(milliseconds_t t) override
    {
        milliseconds_t dt = hasLastT ? (t - lastT) : 16;
        lastT = t;
        hasLastT = true;

        uint8_t blendAmount = accumulateFadeAmount(blendDebt, BLEND_RATE_PER_SECOND, dt);
        currentColor = CRGB::blend(currentColor, targetColor, blendAmount);
    }

    CRGB evaluate(LedStrip* strip, Led* led, size_t led_idx, milliseconds_t t) override
    {
        return currentColor;
    }

    StaticColor(CRGB c) : targetColor(c) {}
    StaticColor() : StaticColor(CRGB(255, 255, 170)) {}
};
