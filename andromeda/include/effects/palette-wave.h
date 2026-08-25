#pragma once

#include "effects-base.h"
#include "effects-utils.h"
#include "utils.h"

class PaletteWave : public AbstractEffect
{
   public:
    virtual const char* GetName() { return "PaletteWave"; }

    CRGBPalette256 palette;
    RandParam<uint8_t, 3, 8> bpm;
    RandParam<int, 5, 10> scale;

    PaletteWave()
    {
        controlHints |= ControlHints::ROTATE_SPACE;
        CRGBPalette16 palette16 = randomPredefinedPalette();
        UpscalePalette(palette16, palette);
    }

    CRGB evaluate(LedStrip* strip, Led* led, size_t led_idx, milliseconds_t t) override
    {
        int v = (led->cartesian.x + led->cartesian.y) / (int)scale;
        uint8_t value = beatsin8(bpm, 0, 255, 0, v);
        return ColorFromPalette(palette, value);
    }
};
