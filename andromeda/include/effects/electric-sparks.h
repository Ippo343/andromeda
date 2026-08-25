#pragma once

#include <vector>

#include "effects-base.h"
#include "energy-param.h"
#include "utils.h"

using std::vector;

class ElectricSparks : public AbstractEffect
{
   public:
    virtual const char* GetName() { return "ElectricSparks"; }

    // Palette values for each LED
    // Needs double buffering to correctly compute the averaging of neighbouring pixels
    vector<vector<uint8_t>> preValues;
    vector<vector<uint8_t>> newValues;

    CRGBPalette256 palette;

    // Time scale factor for the perlin random noise
    RandParam<unsigned short, 200, 2000> hueTimeScale;

    // Current base hue (updated based on perlin noise)
    uint8_t hue;

    // This is tricky to figure out if not by trial and error.
    // We roll a dice every frame for every led, so the chance must be really small.
    // These are values that I like experimentally, I cannot justify them.
    // TODO: better way to define the frequency
    constexpr static unsigned int DICE_LIMIT = 100000;
    EnergyParam<int, 5, 26> sparkChance;

    // Chance that a spark becomes bigger, rolled out of 100.
    // If the roll is successful, the width is doubled and then rolled again until it fails.
    // Potentially going up to the full strip in rare cases.
    EnergyParam<int, 40, 70> bigSparkChance;

    ElectricSparks()
    {
        // Allocate vectors for each strip
        preValues.resize(GEOMETRY.getNumStrips());
        newValues.resize(GEOMETRY.getNumStrips());

        for (size_t i = 0; i < GEOMETRY.getNumStrips(); i++)
        {
            preValues[i].resize(GEOMETRY.getStrip(i).num_leds, 0);
            newValues[i].resize(GEOMETRY.getStrip(i).num_leds, 0);
        }

        hue = random(0, 256);
        updatePalette();
    }

    inline uint8_t avg38(int a, int b, int c) { return constrain((a + b + c) / 3, 0, 255); }

    void updatePalette()
    {
        // Create the palette based on the current hue.
        // - the first color at index 0 is the current hue, fully saturated;
        // - the last color is the complementary color of the base color, but partially desaturated
        // - the 1/3 color is the same as the base color, but half-saturated;
        // - the 2/3 color is pure white/
        // This logic was picked by trial and error and looks pretty nice.

        CHSV base = CHSV(hue, 255, 255);
        CHSV comp = CHSV(((short)hue + 128) % 256, 128, 255);
        CHSV desat = base;
        desat.s = 128;
        CHSV white = CHSV(0, 0, 255);

        CHSVPalette16 paletteTemp(base, desat, white, comp);

        // Upscale the palette so no interpolation is needed while running.
        // gives a completely imperceptible performace boost.
        UpscalePalette(paletteTemp, palette);
    }

    void precompute(milliseconds_t t) override
    {
        EVERY_N_MILLISECONDS(100)
        {
            hue = inoise8(t / hueTimeScale);
            updatePalette();
        }

        FOR_EACH_STRIP
        {
            FOR_EACH_LED(iStrip)
            {
                newValues[iStrip][iLed] =
                    avg38(py_get(preValues[iStrip], iLed - 1), preValues[iStrip][iLed],
                          py_get(preValues[iStrip], iLed + 1));
            }
        }
    }

    CRGB evaluate(LedStrip* strip, Led* led, size_t led_idx, milliseconds_t t) override
    {
        // Random injection of new spikes
        if (random(DICE_LIMIT) < sparkChance)
        {
            int width = 1;

            // Keep rolling for a chance to increase the spark's size
            while (random(100) < bigSparkChance) width *= 2;

            // Now light up the pixel and its neighbours up to the defined width
            for (size_t w = 0; w < width; w++)
            {
                py_get(newValues[strip->idx], led_idx + w) = 255;
                py_get(newValues[strip->idx], led_idx - w) = 255;
            }
        }

        return ColorFromPalette(palette, preValues[strip->idx][led_idx]);
    }

    void postprocess(milliseconds_t t) override
    {
        // Dissipate the energy to lower values
        FOR_EACH_STRIP
        {
            size_t stripLen = GEOMETRY.getStrip(iStrip).num_leds;
            for (size_t iLed = 0; iLed < stripLen; iLed++)
            {
                newValues[iStrip][iLed] = scale8(newValues[iStrip][iLed], 254);
            }
        }

        // Copy the current buffer so that the next frame can diffuse it
        preValues = newValues;
    }
};
