#pragma once

#include <FastLED.h>

// Calibrates the user-facing brightness slider to what the device can actually
// deliver inside its power budget (#237).
//
// main.cpp installs FastLED.setMaxPowerInMilliWatts() from the model's
// max_milliamps: FastLED estimates every frame's draw from the rendered colors
// and globally dims to stay under it. That limiter is content-dependent, so
// past a certain slider position raising the request raises nothing - the
// limiter cancels it one-for-one and the top of the travel is dead. On an L10
// (52 LEDs, 650mA after #216) a full-white frame hits the budget at an applied
// brightness of ~76/255, killing roughly the top half of the slider.
//
// The fix is a static ceiling, computed once at boot: pick a reference frame
// per model (ModelConfig::brightness_reference_level), work out the highest
// brightness at which that frame still fits the budget, and map the whole
// 0-255 slider onto 0..ceiling. Every slider position then changes the output,
// and brightness at a given position stays put as effects come and go - a
// per-frame headroom mapping would instead pump the gain up and down with
// content. FastLED's limiter stays installed exactly as it was, demoted to a
// pure safety net for frames brighter than the reference load.

// The highest FastLED brightness at which a frame drawing
// referenceMilliwattsAt255 (as reported by calculate_unscaled_power_mW(), i.e.
// at brightness 255) still fits inside budgetMilliwatts. The inverse of
// power-monitor.h's estimateCurrentMa(), and deliberately derived from the
// same per-LED FastLED estimator the limiter itself uses - though not an
// exact inverse of it: FastLED's real limiter (calculate_max_brightness_for_
// power_mW's internal overload) also reserves a small fixed MCU allowance
// (~125mW) that isn't exposed by any public API and so can't be replicated
// here. Left out rather than hardcoded, since a private FastLED constant is
// free to drift across library upgrades; on shipped budgets (hundreds of mA
// and up) this is a low single-digit-percent difference, well inside FastLED's
// own documented ~10% approximation - see L10_mk0.cpp's budget derivation.
//
// uint64_t intermediate for the same reason estimateCurrentMa() needs one: the
// reference power of a large panel at full white already eats most of a
// uint32_t's headroom, and scaling it by 255 first (to keep the division exact
// rather than losing precision to an early divide) overflows 32 bits on its own.
inline uint8_t computeBrightnessCeiling(uint32_t referenceMilliwattsAt255,
                                        uint32_t budgetMilliwatts)
{
    // No reference load to calibrate against (an unconfigured model, or a
    // geometry with no LEDs) - leave the slider uncalibrated rather than
    // dividing by zero. Same shape as estimateCurrentMa()'s zero-rail guard (#221).
    if (referenceMilliwattsAt255 == 0) return 255;

    uint64_t ceiling = (uint64_t)budgetMilliwatts * 255 / referenceMilliwattsAt255;

    if (ceiling > 255) return 255;
    // A budget too small to light the reference frame at all still has to leave
    // the device visible: clamp to 1 so no configuration can render pure black.
    if (ceiling < 1) return 1;
    return (uint8_t)ceiling;
}

// Maps a user-facing 0-255 slider value onto 0..ceiling, preserving the gamma
// curve the slider has always been read through.
//
// scale8_video, not scale8: dim8_raw already crushes the low end hard, and a
// plain scale8 on top of it would round a small-but-deliberate request down to
// hard 0. A ceiling of 255 is an exact passthrough of the old dim8_raw()
// behavior, so a model that needs no calibration is byte-identical to before.
inline uint8_t applyBrightnessCeiling(uint8_t requested, uint8_t ceiling)
{
    return scale8_video(dim8_raw(requested), ceiling);
}
