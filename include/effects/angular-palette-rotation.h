#pragma once

// Colour every LED by its polar angle through a palette, and rotate that mapping
// slowly and continuously. On the L10 this is a gradient sweeping around the
// square frame (and, because a square's corners cover angle faster than its
// sides, the sweep naturally speeds up and slows down as it goes); on a radial
// board it's a turning pinwheel. Pure function of angle and absolute time - no
// per-frame state, so it stays perfectly smooth at any frame rate. Works on
// every model unchanged.

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
    }

    CRGB evaluate(LedStrip* strip, Led* led, size_t led_idx, milliseconds_t t) override
    {
        uint16_t ang = led->polar.cdegrees % FULL_CIRCLE;
        uint8_t a = (uint8_t)((uint32_t)ang * 256u / FULL_CIRCLE);
        return ColorFromPalette(palette_, (uint8_t)(offset_ + a * (uint8_t)wraps_));
    }

   private:
    CRGBPalette256 palette_;
    RandParam<uint8_t, 1, 3> wraps_;       // palette repeats per full turn
    RandParam<uint8_t, 4, 7> speedShift_;  // rotation speed (higher = slower)
    RandSign dir_;
    uint8_t offset_ = 0;
};
