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

    // Frame-rate independent blend towards targetColor. The approach is modelled as
    // exponential decay of the remaining distance: after SETTLE_SECONDS the displayed colour
    // is within ~1/255 of the target, i.e. visually indistinguishable. accumulateFadeAmount()
    // turns the rate into a per-frame CRGB::blend() amount via dt, so a colour change takes
    // the same wall-clock time to converge regardless of frame rate.
    constexpr static float SETTLE_SECONDS = 0.8f;
    // ln(255) ~= 5.54: the decay constant that closes all but 1/255 of the gap in
    // SETTLE_SECONDS. (The old value of 0.235f settled in ~24s, which felt like a crawl.)
    constexpr static float BLEND_RATE_PER_SECOND = 5.541f / SETTLE_SECONDS;
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
