#include <unity.h>

#include "brightness-ceiling.h"
#include "geometry/model_registry.h"

void test_brightness_ceiling_setUp() {}
void test_brightness_ceiling_tearDown() {}

void test_compute_ceiling_is_255_when_reference_already_fits_at_full_brightness()
{
    // Reference frame draws less than the budget even at 255 - no calibration needed.
    TEST_ASSERT_EQUAL_UINT8(255, computeBrightnessCeiling(1000, 2000));
}

void test_compute_ceiling_is_255_when_reference_exactly_matches_budget()
{
    TEST_ASSERT_EQUAL_UINT8(255, computeBrightnessCeiling(2000, 2000));
}

void test_compute_ceiling_halves_when_reference_is_double_the_budget()
{
    // Reference frame draws 2x the budget at 255 -> the slider tops out around
    // half brightness. 255 * 1 / 2 = 127 (integer truncation).
    TEST_ASSERT_EQUAL_UINT8(127, computeBrightnessCeiling(2000, 1000));
}

void test_compute_ceiling_matches_the_l10_derivation()
{
    // L10 MK0: 52 LEDs, reference level 128, budget 650mA on a 5V rail.
    // Mirrors main.cpp's own arithmetic: reference mW = FastLED's estimate for
    // one CRGB(128,128,128) pixel, scaled by LED count; budget mW = 650mA * 5000mV / 1000.
    CRGB pixel(128, 128, 128);
    uint32_t referenceMw = calculate_unscaled_power_mW(&pixel, 1) * 52;
    uint32_t budgetMw = (uint32_t)650 * 5000 / 1000;
    uint8_t ceiling = computeBrightnessCeiling(referenceMw, budgetMw);
    // Sanity: brighter than the ~76/255 the old uncalibrated L10 plateaued at,
    // and not simply 255 (calibration must actually be doing something).
    TEST_ASSERT_GREATER_THAN_UINT8(76, ceiling);
    TEST_ASSERT_LESS_THAN_UINT8(255, ceiling);
}

void test_compute_ceiling_clamps_to_one_never_zero()
{
    // A budget far too small for the reference frame must still leave the
    // device visible rather than computing to hard 0.
    TEST_ASSERT_EQUAL_UINT8(1, computeBrightnessCeiling(1000000, 1));
}

void test_compute_ceiling_zero_reference_is_uncalibrated()
{
    // No reference load (e.g. a misconfigured model, or zero LEDs) - leave the
    // slider as-is rather than dividing by zero. Mirrors estimateCurrentMa()'s
    // zero-rail guard (#221).
    TEST_ASSERT_EQUAL_UINT8(255, computeBrightnessCeiling(0, 2000));
}

void test_compute_ceiling_does_not_overflow_on_a_large_panel_and_budget()
{
    // A large panel's reference power and a large budget must not overflow the
    // uint64_t intermediate before the final divide.
    uint32_t large = 4000000000u;
    TEST_ASSERT_EQUAL_UINT8(255, computeBrightnessCeiling(large, large));
    TEST_ASSERT_EQUAL_UINT8(127, computeBrightnessCeiling(large, large / 2));
}

void test_apply_ceiling_255_is_a_pure_dim8_raw_passthrough()
{
    // A ceiling of 255 - the default for any model that needs no calibration -
    // must be byte-identical to the old, uncalibrated dim8_raw() behavior.
    for (int i = 0; i <= 255; i++)
        TEST_ASSERT_EQUAL_UINT8(dim8_raw((uint8_t)i), applyBrightnessCeiling((uint8_t)i, 255));
}

void test_apply_ceiling_zero_request_is_zero()
{
    TEST_ASSERT_EQUAL_UINT8(0, applyBrightnessCeiling(0, 128));
    TEST_ASSERT_EQUAL_UINT8(0, applyBrightnessCeiling(0, 255));
}

void test_apply_ceiling_is_monotonic_non_decreasing()
{
    uint8_t ceiling = 100;
    uint8_t prev = applyBrightnessCeiling(0, ceiling);
    for (int i = 1; i <= 255; i++)
    {
        uint8_t cur = applyBrightnessCeiling((uint8_t)i, ceiling);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT8(prev, cur);
        prev = cur;
    }
}

void test_apply_ceiling_a_small_nonzero_request_does_not_round_to_zero()
{
    // dim8_raw(16) == 1 (a genuinely tiny but nonzero brightness - dim8_raw's
    // own gamma crush is what's responsible for a *smaller* raw request like
    // 1 already flattening to 0, independent of any ceiling). scale8_video
    // (not scale8) then guarantees that nonzero 1 never scales down to hard 0
    // against a reduced ceiling.
    TEST_ASSERT_EQUAL_UINT8(1, dim8_raw(16));
    TEST_ASSERT_NOT_EQUAL_UINT8(0, applyBrightnessCeiling(16, 128));
}

void test_every_registered_model_has_a_nonzero_brightness_reference_level()
{
    // Guards against a future ModelConfig shipping brightness_reference_level
    // == 0: computeBrightnessCeiling() treats a zero reference as "no
    // calibration data" and returns 255 (see its own zero guard), which would
    // silently defeat this field rather than actually calibrating anything.
    for (size_t i = 0; i < NUM_MODELS; i++)
        TEST_ASSERT_GREATER_THAN_UINT8(0, MODEL_REGISTRY[i]->brightness_reference_level);
}

void run_test_brightness_ceiling_tests()
{
    RUN_TEST(test_compute_ceiling_is_255_when_reference_already_fits_at_full_brightness);
    RUN_TEST(test_compute_ceiling_is_255_when_reference_exactly_matches_budget);
    RUN_TEST(test_compute_ceiling_halves_when_reference_is_double_the_budget);
    RUN_TEST(test_compute_ceiling_matches_the_l10_derivation);
    RUN_TEST(test_compute_ceiling_clamps_to_one_never_zero);
    RUN_TEST(test_compute_ceiling_zero_reference_is_uncalibrated);
    RUN_TEST(test_compute_ceiling_does_not_overflow_on_a_large_panel_and_budget);
    RUN_TEST(test_apply_ceiling_255_is_a_pure_dim8_raw_passthrough);
    RUN_TEST(test_apply_ceiling_zero_request_is_zero);
    RUN_TEST(test_apply_ceiling_is_monotonic_non_decreasing);
    RUN_TEST(test_apply_ceiling_a_small_nonzero_request_does_not_round_to_zero);
    RUN_TEST(test_every_registered_model_has_a_nonzero_brightness_reference_level);
}
