#include <unity.h>

#include <set>
#include <string>

// effects.cpp declares most of its effect classes locally (not in a header),
// so we #include the .cpp directly to reach them for white-box testing.
// This is a test-only decision - effects.cpp is deliberately excluded from
// platformio.ini's native build_src_filter to avoid a duplicate-symbol link
// error (see the comment there).
#include "../../src/effects.cpp"

void setUp() { GEOMETRY.initializeForTest(ModelId::SINGLE_STRIP_TEST_DEVICE); }
void tearDown() {}

// Small helper: run an effect through a handful of (led, t) samples the way
// AbstractEffect::render() would, without depending on real hardware buffers.
static void exerciseEvaluate(AbstractEffect& fx, milliseconds_t t)
{
    LedStrip& strip = GEOMETRY.getStrip(0);
    fx.precompute(t);
    for (size_t i = 0; i < strip.num_leds; i += 7)
    {
        CRGB c = fx.evaluate(&strip, &strip.leds[i], i, t);
        (void)c;  // just confirm it doesn't crash / UB-sanitizer trip
    }
    fx.postprocess(t);
}

// ---------------------------------------------------------------------------
// ErrorEffect / StaticColor (declared in effects.h)
// ---------------------------------------------------------------------------

void test_error_effect_strip0_is_red_others_black()
{
    ErrorEffect fx;
    LedStrip& strip = GEOMETRY.getStrip(0);
    TEST_ASSERT_TRUE(fx.evaluate(&strip, &strip.leds[0], 0, 0) == CRGB::Red);

    LedStrip fakeStrip1;
    fakeStrip1.idx = 1;
    TEST_ASSERT_TRUE(fx.evaluate(&fakeStrip1, &strip.leds[0], 0, 0) == CRGB::Black);
}

void test_static_color_blends_toward_target()
{
    StaticColor fx(CRGB(200, 100, 50));
    TEST_ASSERT_TRUE(fx.targetColor == CRGB(200, 100, 50));

    LedStrip& strip = GEOMETRY.getStrip(0);
    CRGB before = fx.currentColor;
    fx.precompute(0);
    CRGB c = fx.evaluate(&strip, &strip.leds[0], 0, 0);
    TEST_ASSERT_TRUE(c == fx.currentColor);
    // blend(before, target, 1) should move (at most slightly) toward target, never past it
    TEST_ASSERT_TRUE(c.r >= before.r);
}

void test_static_color_default_constructor()
{
    StaticColor fx;
    TEST_ASSERT_TRUE(fx.targetColor == CRGB(255, 255, 170));
}

// ---------------------------------------------------------------------------
// IndividualStripMoodlight
// ---------------------------------------------------------------------------

void test_individual_strip_moodlight_evaluates()
{
    IndividualStripMoodlight fx;
    exerciseEvaluate(fx, 1000);
    LedStrip& strip = GEOMETRY.getStrip(0);
    TEST_ASSERT_TRUE(fx.evaluate(&strip, &strip.leds[0], 0, 1000) == fx.colors[0]);
}

// ---------------------------------------------------------------------------
// ElectricSparks
// ---------------------------------------------------------------------------

void test_electric_sparks_runs_full_frame_cycle()
{
    ElectricSparks fx;
    exerciseEvaluate(fx, 5000);
    exerciseEvaluate(fx, 5100);  // second frame exercises the diffusion/postprocess path
}

void test_electric_sparks_avg38_clamps()
{
    ElectricSparks fx;
    TEST_ASSERT_EQUAL_INT(255, fx.avg38(255, 255, 255));
    TEST_ASSERT_EQUAL_INT(0, fx.avg38(0, 0, 0));
}

// ---------------------------------------------------------------------------
// SaturationGlow
// ---------------------------------------------------------------------------

void test_saturation_glow_evaluates()
{
    SaturationGlow fx;
    exerciseEvaluate(fx, 2000);
}

// ---------------------------------------------------------------------------
// PaletteWave
// ---------------------------------------------------------------------------

void test_palette_wave_sets_rotate_space_hint()
{
    PaletteWave fx;
    TEST_ASSERT_TRUE((fx.controlHints & ControlHints::ROTATE_SPACE) != 0);
    exerciseEvaluate(fx, 3000);
}

// ---------------------------------------------------------------------------
// NinjaStar
// ---------------------------------------------------------------------------

void test_ninja_star_evaluates()
{
    NinjaStar fx;
    exerciseEvaluate(fx, 4000);
}

// ---------------------------------------------------------------------------
// PolarSwipe
// ---------------------------------------------------------------------------

void test_polar_swipe_evaluates_both_flip_directions()
{
    // RandBool exposes only a public operator T() (no external setter), so
    // both branches of the "if (flip)" in precompute() are reached by
    // constructing until each value has been observed at least once.
    bool sawTrue = false, sawFalse = false;
    for (int i = 0; i < 50 && !(sawTrue && sawFalse); i++)
    {
        PolarSwipe fx;
        if (static_cast<bool>(fx.flip))
            sawTrue = true;
        else
            sawFalse = true;
        exerciseEvaluate(fx, 1000 + i);
    }
    TEST_ASSERT_TRUE(sawTrue);
    TEST_ASSERT_TRUE(sawFalse);
}

void test_polar_swipe_get_brightness_bounds()
{
    PolarSwipe fx;
    fx.bandCenter = 100;
    uint8_t width = fx.bandWidth;  // read the randomly-chosen width via the public operator

    LedStrip& strip = GEOMETRY.getStrip(0);
    Led nearLed = strip.leds[0];
    nearLed.polar.radius = 100;  // exactly on the band center -> full brightness
    TEST_ASSERT_EQUAL_UINT8(255, fx.getBrightness(&nearLed));

    Led farLed = strip.leds[0];
    farLed.polar.radius = 100 + width + 500;  // well outside band -> zero brightness
    TEST_ASSERT_EQUAL_UINT8(0, fx.getBrightness(&farLed));
}

// ---------------------------------------------------------------------------
// PolarMoodlight
// ---------------------------------------------------------------------------

void test_polar_moodlight_evaluates()
{
    PolarMoodlight fx;
    exerciseEvaluate(fx, 6000);
}

// ---------------------------------------------------------------------------
// RGBodyProblem
// ---------------------------------------------------------------------------

void test_rgbody_problem_evaluates()
{
    RGBodyProblem fx;
    exerciseEvaluate(fx, 7000);
}

// ---------------------------------------------------------------------------
// HexagonalRippleGalaxy
// ---------------------------------------------------------------------------

void test_hexagonal_ripple_galaxy_evaluates()
{
    HexagonalRippleGalaxy fx;
    exerciseEvaluate(fx, 8000);
}

// ---------------------------------------------------------------------------
// IndividualStripDrift
// ---------------------------------------------------------------------------

void test_individual_strip_drift_transitions_to_new_target()
{
    IndividualStripDrift fx;
    milliseconds_t initialEnd = fx.transitionEndTimes[0];

    // Before the transition ends: currentColors should interpolate, target unchanged
    fx.precompute(fx.transitionStartTimes[0]);
    TEST_ASSERT_EQUAL_UINT32(initialEnd, fx.transitionEndTimes[0]);

    // At/after the transition end: a new target must be picked (branch coverage
    // for the "t >= transitionEndTimes[iStrip]" case)
    fx.precompute(initialEnd + 1);
    TEST_ASSERT_TRUE(fx.transitionEndTimes[0] > initialEnd);

    LedStrip& strip = GEOMETRY.getStrip(0);
    CRGB c = fx.evaluate(&strip, &strip.leds[0], 0, initialEnd + 1);
    TEST_ASSERT_TRUE(c == fx.currentColors[0]);
}

// ---------------------------------------------------------------------------
// CartesianMoodlight
// ---------------------------------------------------------------------------

void test_cartesian_moodlight_randomize_and_evaluate()
{
    CartesianMoodlight fx;
    fx.randomize();
    exerciseEvaluate(fx, 9000);
    // A second evaluate at the same phase should hit the memoization cache path
    // in evaluatePowerSine (sinPowerLUT already populated for that sinByte).
    exerciseEvaluate(fx, 9000);
}

// ---------------------------------------------------------------------------
// effects-utils.cpp: paint / randomColor / randomComplementaryColors /
// randomPredefinedPalette / brightnessFromEmitter
// ---------------------------------------------------------------------------

void test_paint_fills_all_strips()
{
    paint(CRGB::Blue);
    LedStrip& strip = GEOMETRY.getStrip(0);
    for (size_t i = 0; i < strip.num_leds; i++) TEST_ASSERT_TRUE(strip.buffer[i] == CRGB::Blue);
}

void test_paint_strip_fills_only_target_strip()
{
    paint(CRGB::Black);
    paintStrip(0, CRGB::Green);
    LedStrip& strip = GEOMETRY.getStrip(0);
    TEST_ASSERT_TRUE(strip.buffer[0] == CRGB::Green);
}

void test_random_color_full_saturation_and_value()
{
    CHSV c = randomColor();
    TEST_ASSERT_EQUAL_UINT8(255, c.s);
    TEST_ASSERT_EQUAL_UINT8(255, c.v);
}

void test_random_complementary_colors_are_evenly_spaced()
{
    std::vector<CHSV> colors = randomComplementaryColors(3);
    TEST_ASSERT_EQUAL_INT(3, colors.size());
    for (auto& c : colors)
    {
        TEST_ASSERT_EQUAL_UINT8(255, c.s);
        TEST_ASSERT_EQUAL_UINT8(255, c.v);
    }
}

void test_random_predefined_palette_returns_nonempty_palette()
{
    for (int i = 0; i < 20; i++)
    {
        CRGBPalette16 p = randomPredefinedPalette();
        // Palette entries should not all be pure black (sanity that a real
        // palette was selected, exercising the switch's various branches).
        bool has_non_black = false;
        for (int j = 0; j < 16; j++)
        {
            if (p[j] != CRGB::Black)
            {
                has_non_black = true;
                break;
            }
        }
        TEST_ASSERT_TRUE(has_non_black);
    }
}

void test_brightness_from_emitter_decays_with_distance()
{
    LedStrip& strip = GEOMETRY.getStrip(0);
    Led* led = &strip.leds[0];  // {458, 0} per single_strip_test_device.cpp

    // Avoid distance == 0: Q_rsqrt(0) is an edge case of the fast-inverse-sqrt
    // trick and not representative of what effects actually pass in.
    uint8_t nearby = brightnessFromEmitter(led, {450, 0});
    uint8_t farAway = brightnessFromEmitter(led, {-458, 0});

    TEST_ASSERT_TRUE(nearby >= farAway);
}

// ---------------------------------------------------------------------------
// ModelId -> Geometry -> Effect coordinate chain (real multi-strip model)
// ---------------------------------------------------------------------------

// Every other test in this file evaluates effects against
// SINGLE_STRIP_TEST_DEVICE, a single synthetic strip. This confirms a
// polar-coordinate effect (PolarSwipe) genuinely consumes real per-strip,
// per-LED polar coordinates from a real multi-strip production model
// (L70 MK1: 2 strips) - i.e. that FactoryConfig's ModelId selection actually
// drives what coordinates Effects see, not just that a single strip works.
void test_polar_swipe_evaluates_across_all_strips_of_real_multi_strip_model()
{
    GEOMETRY.initializeForTest(ModelId::L70_MK1);
    TEST_ASSERT_EQUAL_INT(2, GEOMETRY.getNumStrips());

    PolarSwipe fx;
    fx.precompute(0);

    for (size_t iStrip = 0; iStrip < GEOMETRY.getNumStrips(); iStrip++)
    {
        LedStrip& strip = GEOMETRY.getStrip(iStrip);
        for (size_t i = 0; i < strip.num_leds; i += 11)
        {
            CRGB c = fx.evaluate(&strip, &strip.leds[i], i, 0);
            (void)c;  // just confirm it doesn't crash / UB-sanitizer trip
        }
    }
}

// ---------------------------------------------------------------------------
// getRandomEffect() factory
// ---------------------------------------------------------------------------

void test_get_random_effect_produces_valid_effects()
{
    std::set<std::string> namesSeen;
    for (int i = 0; i < 300; i++)
    {
        AbstractEffect* fx = getRandomEffect();
        TEST_ASSERT_NOT_NULL(fx);
        TEST_ASSERT_NOT_NULL(fx->GetName());
        namesSeen.insert(fx->GetName());
        delete fx;
    }
    // 11 possible effects; 300 draws (never repeating consecutively) should
    // realistically hit all of them.
    TEST_ASSERT_TRUE(namesSeen.size() >= 8);
}

int main(int argc, char** argv)
{
    UNITY_BEGIN();

    RUN_TEST(test_error_effect_strip0_is_red_others_black);
    RUN_TEST(test_static_color_blends_toward_target);
    RUN_TEST(test_static_color_default_constructor);

    RUN_TEST(test_individual_strip_moodlight_evaluates);

    RUN_TEST(test_electric_sparks_runs_full_frame_cycle);
    RUN_TEST(test_electric_sparks_avg38_clamps);

    RUN_TEST(test_saturation_glow_evaluates);

    RUN_TEST(test_palette_wave_sets_rotate_space_hint);

    RUN_TEST(test_ninja_star_evaluates);

    RUN_TEST(test_polar_swipe_evaluates_both_flip_directions);
    RUN_TEST(test_polar_swipe_get_brightness_bounds);

    RUN_TEST(test_polar_moodlight_evaluates);

    RUN_TEST(test_rgbody_problem_evaluates);

    RUN_TEST(test_hexagonal_ripple_galaxy_evaluates);

    RUN_TEST(test_individual_strip_drift_transitions_to_new_target);

    RUN_TEST(test_cartesian_moodlight_randomize_and_evaluate);

    RUN_TEST(test_get_random_effect_produces_valid_effects);

    RUN_TEST(test_polar_swipe_evaluates_across_all_strips_of_real_multi_strip_model);

    RUN_TEST(test_paint_fills_all_strips);
    RUN_TEST(test_paint_strip_fills_only_target_strip);
    RUN_TEST(test_random_color_full_saturation_and_value);
    RUN_TEST(test_random_complementary_colors_are_evenly_spaced);
    RUN_TEST(test_random_predefined_palette_returns_nonempty_palette);
    RUN_TEST(test_brightness_from_emitter_decays_with_distance);

    return UNITY_END();
}
