#pragma once

#include "effects-base.h"
#include "effects-utils.h"
#include "utils.h"

// Keeps swiping and fading a color through the radius
class PolarSwipe : public AbstractEffect
{
   public:
    virtual const char* GetName() { return "Polar Swipe"; }

    RandBool flip;
    RandParam<uint8_t, 10, 40> bpm;
    RandParam<uint8_t, 20, 80> bandWidth;

    // Minimum and maximum radii for the swipe.
    // It needs to start from aperture / 2 so that the minimum of the band is at 0,
    // and it needs to finish at the edge of the screen + aperture for the same reason.
    //
    // And then finally you need a 1mm buffer: we need to push the band completely outside
    // of the screen, because when that is out the color is chosen randomly.
    // But without this buffer, the LEDs at the very edge of the structure are technically
    // just inside the band, and so they get a new random color for a few consecutive frames
    // causing an annoying color flicker at the edge.
    //
    unsigned short scanMin = bandWidth / 2;
    unsigned short scanMax = GEOMETRY.getScreenRadius() + (bandWidth + 1);

    unsigned short bandCenter;
    CRGB color;

    PolarSwipe() { color = randomColor(); }

    void precompute(milliseconds_t t) override
    {
        unsigned short v = beat16(bpm);

        if (flip)
            bandCenter = map(v, 0, 65535, scanMax, scanMin);
        else
            bandCenter = map(v, 0, 65535, scanMin, scanMax);

        if (bandCenter >= GEOMETRY.getScreenRadius() + bandWidth) color = randomColor();
    }

    inline uint8_t getBrightness(Led* led)
    {
        unsigned short R = led->polar.radius;
        unsigned short D = abs(R - bandCenter);

        if (D > bandWidth)
            return 0;
        else
            return map(D, 0, bandWidth, 255, 0);
    }

    CRGB evaluate(LedStrip* strip, Led* led, size_t led_idx, milliseconds_t t) override
    {
        // The central strip is excluded on the Andromeda mirror because honestly it just looks
        // weird, it adds a sort of sudden "pop" that looks ugly.
        if (GEOMETRY.getConfig()->isInFamily(FamilyID::ANDROMEDA) && strip->idx == 0)
            return CRGB::Black;

        return color % getBrightness(led);
    }
};
