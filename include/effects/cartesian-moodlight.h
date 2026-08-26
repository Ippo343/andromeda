#pragma once

#include "effects-base.h"
#include "utils.h"

// Moodlight with three color waves propagating in random cartesian directions
// Optimized with integer math and memoization
class CartesianMoodlight : public AbstractEffect
{
   public:
    virtual const char* GetName() { return "CartesianMoodlight"; }

    // Tunable parameters
    uint8_t valleyPower = 5;    // Power for stretching valleys (3-7 recommended)
    uint8_t maxWavelength = 5;  // Maximum crests per screen (1-10 range)

    // Random amplitude factors for color variation (scaled so max = 255)
    uint8_t redAmp, greenAmp, blueAmp;

    // Random temporal frequencies (BPM) for each color, stored as accum88
    RandParam<uint8_t, 1, 15> redBpm;
    RandParam<uint8_t, 1, 15> greenBpm;
    RandParam<uint8_t, 1, 15> blueBpm;

    // Precomputed direction vectors scaled by 256 for integer math
    short redDx, redDy;
    short greenDx, greenDy;
    short blueDx, blueDy;

    // Random wavelength scale factors stored as accum88 (8.8 fixed point)
    // Range: 0.5-2.0 crests per screen for long, sweeping waves
    accum88 redWavelength;
    accum88 greenWavelength;
    accum88 blueWavelength;

    // Memoization LUT for sin16 → power stretch
    // 256 entries, one for each possible 8-bit input
    uint8_t sinPowerLUT[256];

    CartesianMoodlight()
    {
        // Initialize LUT with sentinel value
        memset(sinPowerLUT, 0xFF, 256);
    }

    void randomize()
    {
        // Random BPM for temporal variation
        redBpm.randomize();
        greenBpm.randomize();
        blueBpm.randomize();

        // Generate random angles and precompute scaled direction vectors
        randomizeDirection(redDx, redDy);
        randomizeDirection(greenDx, greenDy);
        randomizeDirection(blueDx, blueDy);

        // Random wavelengths: 0.3 to maxWavelength crests per screen (wider variation)
        // Store as accum88: multiply float by 256
        redWavelength = random(30, maxWavelength * 100 + 1) * 256 / 100;
        greenWavelength = random(30, maxWavelength * 100 + 1) * 256 / 100;
        blueWavelength = random(30, maxWavelength * 100 + 1) * 256 / 100;

        // Generate random amplitude factors and scale so max = 255
        uint8_t r = random(256);
        uint8_t g = random(256);
        uint8_t b = random(256);

        uint8_t maxVal = max(r, max(g, b));
        uint8_t boost = 255 - maxVal;

        redAmp = r + boost;
        greenAmp = g + boost;
        blueAmp = b + boost;

        // Clear memoization cache
        memset(sinPowerLUT, 0xFF, 256);
    }

    CRGB evaluate(LedStrip* strip, Led* led, size_t led_idx, milliseconds_t t) override
    {
        // Compute dot products: direction · position
        // Results are in units of (mm * 256), divide by 256 for final distance
        int16_t redDist = ((long)redDx * led->cartesian.x + (long)redDy * led->cartesian.y) >> 8;
        int16_t greenDist =
            ((long)greenDx * led->cartesian.x + (long)greenDy * led->cartesian.y) >> 8;
        int16_t blueDist = ((long)blueDx * led->cartesian.x + (long)blueDy * led->cartesian.y) >> 8;

        // Calculate phase for each color using integer math
        // Spatial: distance * wavelength (both in fixed point)
        // Temporal: time * bpm * scaling factor
        uint16_t redPhase = computePhase(redDist, redWavelength, t, redBpm);
        uint16_t greenPhase = computePhase(greenDist, greenWavelength, t, greenBpm);
        uint16_t bluePhase = computePhase(blueDist, blueWavelength, t, blueBpm);

        // Evaluate with memoized power sine and apply amplitude factors
        uint8_t R = (evaluatePowerSine(redPhase) * redAmp) >> 8;
        uint8_t G = (evaluatePowerSine(greenPhase) * greenAmp) >> 8;
        uint8_t B = (evaluatePowerSine(bluePhase) * blueAmp) >> 8;

        return CRGB(R, G, B);
    }

   private:
    // Generate a random unit direction vector, scaled by 256 for integer math
    void randomizeDirection(short& dx, short& dy)
    {
        // Random angle in degrees (0-359)
        int angle = random(360);

        // Convert to radians and compute direction
        float rad = angle * PI / 180.0;
        dx = (short)(cos(rad) * 256.0);
        dy = (short)(sin(rad) * 256.0);
    }

    // Compute phase as uint16_t using integer math
    uint16_t computePhase(int16_t distance, accum88 wavelength, milliseconds_t t, uint8_t bpm)
    {
        // Spatial component:
        // distance: -260 to 260 mm
        // wavelength: 128 to 512 (0.5 to 2.0 in accum88)
        // We want: at wavelength=1.0 (256), distance=260 → phase ≈ 32768 (half cycle)
        // distance * wavelength = 260 * 256 = 66560
        // We want 32768, so multiply by 0.5: (distance * wavelength) >> 1
        int32_t spatial = ((int32_t)distance * wavelength) >> 1;

        // Temporal component: one full sine period (65536) per beat
        // At N BPM: 60000/N ms per beat
        // phase per ms = 65536 * bpm / 60000 ≈ bpm * 1.092
        // Using integer: (t * bpm * 70) >> 6
        int32_t temporal = ((int32_t)t * bpm * 70) >> 6;

        // Combine - both are now in the right scale for uint16_t
        uint16_t phase = (uint16_t)(spatial + temporal);

        return phase;
    }

    // Evaluate sin16 with power stretch and memoization
    uint8_t evaluatePowerSine(uint16_t phase)
    {
        // Get sin16 output (-32767 to 32767) and convert to uint8_t (0-255)
        int16_t sinVal = sin16(phase);
        uint8_t sinByte = (sinVal + 32768) >> 8;  // Map to 0-255

        // Check memoization cache
        if (sinPowerLUT[sinByte] != 0xFF) { return sinPowerLUT[sinByte]; }

        // Compute power stretch: normalize to 0-1, raise to power, map to 0-255
        // Using integer math with 16-bit precision
        uint16_t normalized = (uint16_t)sinByte << 8;  // Scale to 0-65535
        uint32_t result = normalized;

        // Raise to power by repeated multiplication
        for (size_t i = 1; i < valleyPower; i++) { result = (result * normalized) >> 16; }

        uint8_t finalResult = result >> 8;  // Back to 0-255

        // Store in cache and return
        sinPowerLUT[sinByte] = finalResult;
        return finalResult;
    }
};
