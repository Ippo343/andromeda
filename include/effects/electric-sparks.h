#pragma once

#include <algorithm>
#include <vector>

#include "effects-base.h"
#include "energy-param.h"
#include "physics/frame-clock.h"
#include "utils.h"

using std::vector;

class ElectricSparks : public AbstractEffect
{
   public:
    virtual const char* GetName() { return "Electric Sparks"; }

    // Palette values for each LED
    // Needs double buffering to correctly compute the averaging of neighbouring pixels
    vector<vector<uint8_t>> preValues;
    vector<vector<uint8_t>> newValues;

    CRGBPalette256 palette;

    // Time scale factor for the perlin random noise
    RandParam<unsigned short, 200, 2000> hueTimeScale;

    // Current base hue (updated based on perlin noise)
    uint8_t hue;

    // Frame-rate independent spark injection: sparkRateMilliHz is the expected number of
    // new sparks per LED per second, in thousandths (i.e. rate = sparkRateMilliHz/1000.0).
    // Each frame we convert it, via the actual elapsed dt, into a threshold for a single
    // random(DICE_LIMIT) roll per LED - see rateToThreshold(). These bounds carry over the
    // same tuned digits as the old per-tick chance (5-26), just reinterpreted as a rate.
    constexpr static unsigned int DICE_LIMIT = 100000;
    EnergyParam<int, 5, 26> sparkRateMilliHz;
    uint32_t sparkThreshold = 0;

    // Chance that a spark becomes bigger, rolled out of 100.
    // If the roll is successful, the width is doubled and then rolled again until it fails.
    // Potentially going up to the full strip in rare cases.
    // Not frame-rate coupled: this only rolls once a spark has already fired, so its
    // expected value doesn't depend on how often evaluate() is called.
    EnergyParam<int, 40, 70> bigSparkChance;

    // Frame-rate independent energy dissipation: fraction of the accumulated energy lost
    // per second, converted each frame into a scale8 fade amount via dt - see
    // accumulateFadeAmount(). Starting point equivalent to the old fixed scale8(x, 254)
    // decay at a ~60fps reference; needs a visual pass on real hardware to retune.
    constexpr static float ENERGY_LOSS_RATE_PER_SECOND = 0.235f;
    float decayDebt = 0;

    // Elapsed time (dt) since the previous frame, used to keep spark injection and energy
    // decay frame-rate independent. Computed once in precompute() and reused in
    // postprocess() (same t for both, one tick).
    FrameClock clock;
    milliseconds_t currentDt = 16;

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
        currentDt = clock.tick(t);

        sparkThreshold = rateToThreshold(sparkRateMilliHz / 1000.0f, currentDt, DICE_LIMIT);

        EVERY_N_MILLISECONDS(100)
        {
            hue = inoise8(t / hueTimeScale);
            updatePalette();
        }

        // Interior LEDs never actually wrap - only the first and last LED of a strip
        // need py_get()'s wraparound modulo (an integer division through a double
        // indirection). Handle those two directly and let the interior loop index
        // straight into the vectors instead of paying that cost on every LED.
        FOR_EACH_STRIP
        {
            size_t stripLen = GEOMETRY.getStrip(iStrip).num_leds;
            if (stripLen == 0) continue;

            vector<uint8_t>& pre = preValues[iStrip];
            vector<uint8_t>& out = newValues[iStrip];

            if (stripLen == 1) { out[0] = avg38(pre[0], pre[0], pre[0]); }
            else
            {
                out[0] = avg38(pre[stripLen - 1], pre[0], pre[1]);
                for (size_t iLed = 1; iLed + 1 < stripLen; iLed++)
                    out[iLed] = avg38(pre[iLed - 1], pre[iLed], pre[iLed + 1]);
                out[stripLen - 1] = avg38(pre[stripLen - 2], pre[stripLen - 1], pre[0]);
            }
        }
    }

    CRGB evaluate(LedStrip* strip, Led* led, size_t led_idx, milliseconds_t t) override
    {
        // Random injection of new spikes
        if (random(DICE_LIMIT) < sparkThreshold)
        {
            size_t width = 1;

            // bigSparkChance is an EnergyParam - reading it maps the global energy value
            // via cmap() on every access. Energy doesn't change mid-roll, so read it once
            // instead of re-running that map/constrain on every loop iteration.
            int bigSparkChanceNow = bigSparkChance;

            // Keep rolling for a chance to increase the spark's size. Expected work here
            // diverges (2 * max bigSparkChance/100 > 1), so an unclamped width can in rare
            // cases double dozens of times before the roll finally fails - clamp to the strip
            // length so a long roll costs at most one pass over the strip instead of a
            // multi-second freeze inside evaluate().
            while (width < strip->num_leds && random(100) < bigSparkChanceNow) width *= 2;
            width = std::min(width, strip->num_leds);

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
        uint8_t fadeAmount =
            accumulateFadeAmount(decayDebt, ENERGY_LOSS_RATE_PER_SECOND, currentDt);

        FOR_EACH_STRIP
        {
            size_t stripLen = GEOMETRY.getStrip(iStrip).num_leds;
            for (size_t iLed = 0; iLed < stripLen; iLed++)
            {
                newValues[iStrip][iLed] = scale8(newValues[iStrip][iLed], 255 - fadeAmount);
            }
        }

        // Swap buffers so the next frame can diffuse what we just wrote, without
        // paying for a deep copy of both nested vectors every frame.
        std::swap(preValues, newValues);
    }
};
