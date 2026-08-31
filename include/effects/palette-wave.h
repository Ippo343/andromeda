#pragma once

#include <vector>

#include "effects-base.h"
#include "effects-utils.h"
#include "utils.h"

class PaletteWave : public AbstractEffect
{
   public:
    virtual const char* GetName() { return "Palette Wave"; }

    CRGBPalette256 palette;
    RandParam<uint8_t, 3, 8> bpm;
    RandParam<int, 5, 10> scale;

    PaletteWave()
    {
        controlHints |= ControlHints::ROTATE_SPACE;
        CRGBPalette16 palette16 = randomPredefinedPalette();
        UpscalePalette(palette16, palette);
    }

    void precompute(milliseconds_t t) override { ensurePerLedCache(); }

    CRGB evaluate(LedStrip* strip, Led* led, size_t led_idx, milliseconds_t t) override
    {
        int v = vCache[strip->idx][led_idx];
        uint8_t value = beatsin8(bpm, 0, 255, 0, v);
        return ColorFromPalette(palette, value);
    }

   private:
    std::vector<std::vector<int>> vCache;
    bool perLedCacheReady = false;

    // (cartesian.x + cartesian.y) / scale is a pure function of a LED's
    // coordinates (and scale, fixed for the effect's lifetime) - no time
    // dependency, so it's precomputed once instead of divided every LED
    // every frame. This effect has the ROTATE_SPACE hint, so its
    // coordinates aren't final until MissionControl::finishTransition()
    // applies the rotation - which happens after construction but before
    // the first precompute() call, hence building the cache lazily here
    // rather than in the constructor.
    void ensurePerLedCache()
    {
        if (perLedCacheReady) return;

        vCache.resize(GEOMETRY.getNumStrips());

        FOR_EACH_STRIP
        {
            LedStrip& strip = GEOMETRY.getStrip(iStrip);
            vCache[iStrip].resize(strip.num_leds);

            FOR_EACH_LED(iStrip)
            {
                Led& led = strip.leds[iLed];
                vCache[iStrip][iLed] = (led.cartesian.x + led.cartesian.y) / (int)scale;
            }
        }

        perLedCacheReady = true;
    }
};
