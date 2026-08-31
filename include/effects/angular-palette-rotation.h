#pragma once

// Colour every LED by its polar angle through a palette, and rotate that mapping
// slowly and continuously. On the L10 this is a gradient sweeping around the
// square frame (and, because a square's corners cover angle faster than its
// sides, the sweep naturally speeds up and slows down as it goes); on a radial
// board it's a turning pinwheel. Pure function of angle and absolute time - no
// per-frame state, so it stays perfectly smooth at any frame rate. Works on
// every model unchanged.

#include <vector>

#include "effects-base.h"
#include "effects-utils.h"
#include "utils.h"

class AngularPaletteRotation : public AbstractEffect
{
   public:
    AngularPaletteRotation()
    {
        CRGBPalette16 p16 = randomPredefinedPalette();
        UpscalePalette(p16, palette_);
    }

    const char* GetName() override { return "Angular Palette Rotation"; }

    void precompute(milliseconds_t t) override
    {
        // Rotation offset derived straight from absolute t: continuous, wraps
        // cleanly mod 256. Larger speedShift_ = slower (one full turn every
        // 256 << speedShift_ ms, i.e. ~4 s to ~33 s).
        offset_ = (uint8_t)((long)(t >> (uint8_t)speedShift_) * (char)dir_);
        ensurePerLedCache();
    }

    CRGB evaluate(LedStrip* strip, Led* led, size_t led_idx, milliseconds_t t) override
    {
        // a*wraps_ is a pure function of a LED's fixed polar angle and
        // wraps_ (itself fixed for the effect's lifetime) - no time
        // dependency, so it's precomputed once instead of remapped and
        // multiplied every LED every frame.
        return ColorFromPalette(palette_, (uint8_t)(offset_ + angleTermCache[strip->idx][led_idx]));
    }

   private:
    CRGBPalette256 palette_;
    RandParam<uint8_t, 1, 3> wraps_;       // palette repeats per full turn
    RandParam<uint8_t, 4, 7> speedShift_;  // rotation speed (higher = slower)
    RandSign dir_;
    uint8_t offset_ = 0;

    std::vector<std::vector<uint8_t>> angleTermCache;
    bool perLedCacheReady = false;

    void ensurePerLedCache()
    {
        if (perLedCacheReady) return;

        angleTermCache.resize(GEOMETRY.getNumStrips());

        FOR_EACH_STRIP
        {
            LedStrip& strip = GEOMETRY.getStrip(iStrip);
            angleTermCache[iStrip].resize(strip.num_leds);

            FOR_EACH_LED(iStrip)
            {
                uint16_t ang = strip.leds[iLed].polar.cdegrees % FULL_CIRCLE;
                uint8_t a = (uint8_t)((uint32_t)ang * 256u / FULL_CIRCLE);
                angleTermCache[iStrip][iLed] = (uint8_t)(a * (uint8_t)wraps_);
            }
        }

        perLedCacheReady = true;
    }
};
