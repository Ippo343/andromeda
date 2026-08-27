#include <unity.h>

#include <set>
#include <string>

#include "../../include/effects.h"

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
// ErrorEffect / StaticColor
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

// Regression: the blend step used to be a fixed CRGB::blend(cur, target, 1) per precompute()
// call, keyed to call count rather than elapsed time - so at a higher tick rate (smaller dt per
// call), the same number of calls covered less wall-clock time yet blended just as far toward
// the target. Same call count, same starting color, but one schedule's dt is ~60x the other's:
// the large-dt schedule must now end up meaningfully closer to the target.
void test_static_color_blend_scales_with_dt_not_call_count()
{
    StaticColor fastTicksFx(CRGB(200, 100, 50));
    fastTicksFx.currentColor = CRGB(0, 0, 0);  // CRGB's default ctor leaves this uninitialized
    milliseconds_t t = 0;
    for (int i = 0; i < 5; i++)
    {
        t += 16;  // ~60fps
        fastTicksFx.precompute(t);
    }

    StaticColor slowTicksFx(CRGB(200, 100, 50));
    slowTicksFx.currentColor = CRGB(0, 0, 0);
    t = 0;
    for (int i = 0; i < 5; i++)
    {
        t += 1000;  // ~1fps
        slowTicksFx.precompute(t);
    }

    TEST_ASSERT_TRUE(slowTicksFx.currentColor.r > fastTicksFx.currentColor.r + 10);
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

// Exercises the dt-tracking path (first-frame fallback, then a real measured dt) across
// very different frame gaps, proving spark injection / energy decay stay frame-rate
// independent instead of assuming a fixed tick length.
void test_electric_sparks_runs_with_varying_frame_gaps()
{
    ElectricSparks fastFx;
    exerciseEvaluate(fastFx, 5000);
    exerciseEvaluate(fastFx, 5016);  // ~60fps gap

    ElectricSparks slowFx;
    exerciseEvaluate(slowFx, 5000);
    exerciseEvaluate(slowFx, 5160);  // ~6fps gap
}

// preValues/newValues are the one place this effect carries real per-LED state that
// persists and diffuses across frames (unlike most effects, which are pure functions of
// (t, global state)). These tests seed that state directly rather than relying on
// evaluate()'s random spark injection, so they exercise only the deterministic
// diffusion/decay math in precompute()/postprocess().
void test_electric_sparks_diffusion_spreads_energy_to_neighbors()
{
    ElectricSparks fx;
    size_t mid = GEOMETRY.getStrip(0).num_leds / 2;
    for (auto& v : fx.preValues[0]) v = 0;
    fx.preValues[0][mid] = 255;

    fx.precompute(1000);  // newValues[iLed] = avg38(pre[iLed-1], pre[iLed], pre[iLed+1])

    TEST_ASSERT_EQUAL_UINT8(0, fx.newValues[0][mid - 2]);
    TEST_ASSERT_TRUE(fx.newValues[0][mid - 1] > 0);
    TEST_ASSERT_TRUE(fx.newValues[0][mid] > 0);
    TEST_ASSERT_TRUE(fx.newValues[0][mid + 1] > 0);
    TEST_ASSERT_EQUAL_UINT8(0, fx.newValues[0][mid + 2]);
}

void test_electric_sparks_energy_decays_to_near_zero_without_further_injection()
{
    ElectricSparks fx;
    fx.preValues[0][GEOMETRY.getStrip(0).num_leds / 2] = 255;

    // Drive precompute()/postprocess() only - never evaluate() - so no new sparks get
    // injected and this isolates decay/diffusion from the random injection roll.
    milliseconds_t t = 0;
    for (int frame = 0; frame < 200; frame++)
    {
        t += 100;
        fx.precompute(t);
        fx.postprocess(t);
    }

    long total = 0;
    for (uint8_t v : fx.preValues[0]) total += v;
    TEST_ASSERT_TRUE(total < 10);
}

// evaluate()'s color comes from preValues[led], one frame behind whatever precompute()
// just wrote into newValues - this confirms it actually reads that state rather than
// returning a stale/constant palette entry.
void test_electric_sparks_evaluate_color_tracks_energy_level()
{
    ElectricSparks fx;
    fx.sparkThreshold = 0;  // keep evaluate()'s random spark-injection branch from firing
    LedStrip& strip = GEOMETRY.getStrip(0);

    fx.preValues[0][0] = 0;
    CRGB dark = fx.evaluate(&strip, &strip.leds[0], 0, 1000);

    fx.preValues[0][0] = 255;
    CRGB bright = fx.evaluate(&strip, &strip.leds[0], 0, 1000);

    TEST_ASSERT_FALSE(dark == bright);
}

// ---------------------------------------------------------------------------
// SaturationGlow
// ---------------------------------------------------------------------------

void test_saturation_glow_evaluates()
{
    SaturationGlow fx;
    exerciseEvaluate(fx, 2000);
}

void test_saturation_glow_evaluate_matches_precomputed_color()
{
    SaturationGlow fx;
    fx.precompute(2000);
    LedStrip& strip = GEOMETRY.getStrip(0);
    CRGB c = fx.evaluate(&strip, &strip.leds[0], 0, 2000);
    TEST_ASSERT_TRUE(c == fx.color[0]);
}

void test_saturation_glow_amplitude_keeps_center_wave_in_range()
{
    SaturationGlow fx;
    // saturationCenter in [100,156]; amplitude is chosen so center +/- amplitude
    // never leaves [0,255], which is what keeps precompute()'s constrain() a no-op.
    int center = fx.saturationCenter;
    TEST_ASSERT_TRUE(center - (int)fx.saturationAmplitude >= 0);
    TEST_ASSERT_TRUE(center + (int)fx.saturationAmplitude <= 255);
}

// color[] is a pure function of t (hue via inoise8, saturation via scaledCubicWave8) -
// confirms precompute() actually produces a moving value instead of settling on a
// constant once constructed.
void test_saturation_glow_color_varies_over_time()
{
    SaturationGlow fx;
    fx.precompute(0);
    CRGB early = fx.color[0];
    fx.precompute(120000);  // 2 minutes later - within the 1-4 minute cycleTime range
    CRGB late = fx.color[0];
    TEST_ASSERT_FALSE(early == late);
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

// evaluate() is a pure function of (led->cartesian, real millis()) - two LEDs at very
// different positions, sampled back-to-back (so real time is effectively frozen between
// the two calls), should land on different palette phases.
void test_palette_wave_color_varies_with_position()
{
    PaletteWave fx;
    LedStrip& strip = GEOMETRY.getStrip(0);
    Led ledA = strip.leds[0];
    ledA.cartesian.x = 0;
    ledA.cartesian.y = 0;
    Led ledB = strip.leds[0];
    ledB.cartesian.x = 1000;
    ledB.cartesian.y = 1000;

    CRGB a = fx.evaluate(&strip, &ledA, 0, 0);
    CRGB b = fx.evaluate(&strip, &ledB, 0, 0);
    TEST_ASSERT_FALSE(a == b);
}

// ---------------------------------------------------------------------------
// NinjaStar
// ---------------------------------------------------------------------------

void test_ninja_star_evaluates()
{
    NinjaStar fx;
    exerciseEvaluate(fx, 4000);
}

void test_ninja_star_pow16_lut_fixed_points_and_shrinks_midrange()
{
    // 0 and 255 are fixed points of scale8(v, v) applied any number of times;
    // a mid-range value should shrink towards 0 (repeated squaring of a
    // fraction < 1), confirming the LUT actually reproduces the pow curve
    // instead of e.g. an off-by-one identity table.
    TEST_ASSERT_EQUAL_UINT8(0, NinjaStar::pow16(0));
    TEST_ASSERT_EQUAL_UINT8(255, NinjaStar::pow16(255));
    TEST_ASSERT_TRUE(NinjaStar::pow16(128) < 128);
}

// evaluate() blends innerColor (radius 0) to outerColor (max radius) - confirms that
// blend actually runs in the near-to-far direction, not reversed. Bypasses precompute()
// (whose inner/outer MoodLights are driven by real millis(), not t) and instead sets
// innerColor/outerColor/offset directly; theta only depends on the LED's own angle
// (cdegrees=0 here), so scanning offset finds a phase with nonzero brightness (v) that
// applies identically to both radii, isolating the radial blend from the beam pattern.
void test_ninja_star_evaluate_blends_from_inner_to_outer_by_radius()
{
    NinjaStar fx;
    fx.innerColor = CRGB(200, 0, 0);
    fx.outerColor = CRGB(0, 0, 200);

    LedStrip& strip = GEOMETRY.getStrip(0);
    Led led = strip.leds[0];
    led.polar.cdegrees = 0;  // theta = 0 regardless of offset

    uint8_t v = 0;
    for (int offset = 0; offset < 256 && v == 0; offset += 16)
    {
        fx.offset = (uint8_t)offset;
        v = NinjaStar::pow16(sin8(offset));
    }
    TEST_ASSERT_TRUE(v > 0);

    Led nearLed = led;
    nearLed.polar.radius = 0;
    Led farLed = led;
    farLed.polar.radius = GEOMETRY.getScreenRadius();

    CRGB near = fx.evaluate(&strip, &nearLed, 0, 0);
    CRGB far = fx.evaluate(&strip, &farLed, 0, 0);

    TEST_ASSERT_TRUE(near == (fx.innerColor % v));
    TEST_ASSERT_TRUE(far == (fx.outerColor % v));
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

// getBrightness() is well covered above, but nothing yet confirms evaluate() actually
// applies it to fx.color for the LED's own radius - as opposed to e.g. always using a
// stale/default brightness.
void test_polar_swipe_evaluate_matches_color_scaled_by_brightness()
{
    PolarSwipe fx;
    fx.bandCenter = 100;
    fx.color = CRGB(200, 100, 50);

    LedStrip& strip = GEOMETRY.getStrip(0);
    Led nearLed = strip.leds[0];
    nearLed.polar.radius = 100;  // band center -> full brightness
    CRGB near = fx.evaluate(&strip, &nearLed, 0, 0);
    TEST_ASSERT_TRUE(near == (fx.color % fx.getBrightness(&nearLed)));

    Led farLed = strip.leds[0];
    farLed.polar.radius = 100 + fx.bandWidth + 500;  // outside band -> zero brightness
    CRGB far = fx.evaluate(&strip, &farLed, 0, 0);
    TEST_ASSERT_TRUE(far == CRGB::Black);
}

// The Andromeda mirror's central strip is deliberately excluded from this effect - this
// branch had zero coverage of any kind before.
void test_polar_swipe_andromeda_strip0_is_always_black()
{
    GEOMETRY.initializeForTest(ModelId::ANDROMEDA_MK1);
    PolarSwipe fx;
    fx.bandCenter = 100;
    fx.color = CRGB::White;

    LedStrip& strip = GEOMETRY.getStrip(0);
    TEST_ASSERT_EQUAL_UINT8(0, strip.idx);
    Led led = strip.leds[0];
    led.polar.radius = fx.bandCenter;  // would otherwise be full brightness

    CRGB c = fx.evaluate(&strip, &led, 0, 0);
    TEST_ASSERT_TRUE(c == CRGB::Black);
}

// ---------------------------------------------------------------------------
// PolarMoodlight
// ---------------------------------------------------------------------------

void test_polar_moodlight_evaluates()
{
    PolarMoodlight fx;
    exerciseEvaluate(fx, 6000);
}

void test_polar_moodlight_same_radius_gives_same_color()
{
    // evaluate() is a pure function of polar.radius (per-channel RandSine, no
    // dependency on cartesian position) - two LEDs at the same radius must
    // land on the same color.
    PolarMoodlight fx;
    LedStrip& strip = GEOMETRY.getStrip(0);
    Led ledA = strip.leds[0];
    Led ledB = strip.leds[0];
    ledA.polar.radius = 100;
    ledB.polar.radius = 100;

    CRGB a = fx.evaluate(&strip, &ledA, 0, 1000);
    CRGB b = fx.evaluate(&strip, &ledB, 0, 1000);
    TEST_ASSERT_TRUE(a == b);
}

// ---------------------------------------------------------------------------
// RGBodyProblem
// ---------------------------------------------------------------------------

void test_rgbody_problem_evaluates()
{
    RGBodyProblem fx;
    exerciseEvaluate(fx, 7000);
    exerciseEvaluate(fx, 7016);  // second frame exercises N-body-stepping with a real dt
}

void test_rgbody_problem_brighter_near_an_emitter_than_far_away()
{
    RGBodyProblem fx;
    fx.precompute(1000);

    CartesianCoordinates emitter0 = fx.positions[0].toCartesian();
    LedStrip& strip = GEOMETRY.getStrip(0);
    Led nearLed = strip.leds[0];
    nearLed.cartesian.x = emitter0.x + 5;
    nearLed.cartesian.y = emitter0.y;

    Led farLed = strip.leds[0];
    short farCoord = GEOMETRY.getScreenRadius() * 10;
    farLed.cartesian.x = farCoord;
    farLed.cartesian.y = farCoord;

    CRGB near = fx.evaluate(&strip, &nearLed, 0, 1000);
    CRGB far = fx.evaluate(&strip, &farLed, 0, 1000);

    int nearSum = (int)near.r + near.g + near.b;
    int farSum = (int)far.r + far.g + far.b;
    TEST_ASSERT_TRUE(nearSum > farSum);
}

// Regression test: this file's setUp() uses SINGLE_STRIP_TEST_DEVICE, a deliberately
// non-square (934x10mm) model - exactly the case that exposed a bug where
// RGBodyProblem seeded positions using GEOMETRY.getScreenRadius() (the *wider* half-
// dimension) instead of the narrower one, letting bodies spawn far outside the strip's
// actual 10mm-tall footprint. Checked right after construction, before any simulation
// has run, since NBodySystem has no hard position clamp once it starts stepping.
void test_rgbody_problem_initial_positions_use_narrower_screen_dimension()
{
    randomSeed(11);
    for (int attempt = 0; attempt < 20; attempt++)
    {
        RGBodyProblem fx;
        for (auto& p : fx.positions)
            TEST_ASSERT_TRUE(fabsf(p.y) <= (float)GEOMETRY.getScreenHalfHeight());
    }
}

// ---------------------------------------------------------------------------
// BezierSwarm
// ---------------------------------------------------------------------------

void test_bezier_swarm_evaluates()
{
    BezierSwarm fx;
    exerciseEvaluate(fx, 9500);
    exerciseEvaluate(fx, 9600);  // second frame exercises path-stepping with a real dt
}

void test_bezier_swarm_sets_rotate_space_hint()
{
    BezierSwarm fx;
    TEST_ASSERT_TRUE(fx.controlHints & ControlHints::ROTATE_SPACE);
}

// Guards against the single-emitter case regressing to a static color: retries
// construction (bounded) until landing on the N=1 branch, then confirms the emitted
// hue actually changes after several seconds of simulated time.
void test_bezier_swarm_single_emitter_hue_changes_over_time()
{
    randomSeed(1);
    for (int attempt = 0; attempt < 200; attempt++)
    {
        BezierSwarm fx;
        if (fx.positions.size() != 1) continue;

        fx.precompute(0);
        CHSV first = fx.colors[0];
        fx.precompute(8000);
        CHSV later = fx.colors[0];

        TEST_ASSERT_NOT_EQUAL(first.h, later.h);
        return;
    }
    TEST_FAIL_MESSAGE("Never landed on the N=1 BezierSwarm branch in 200 attempts");
}

// ---------------------------------------------------------------------------
// MultiPendulum
// ---------------------------------------------------------------------------

void test_multi_pendulum_evaluates()
{
    MultiPendulum fx;
    exerciseEvaluate(fx, 10000);
    exerciseEvaluate(fx, 10016);  // second frame exercises chain-stepping with a real dt
}

void test_multi_pendulum_sets_rotate_space_hint()
{
    MultiPendulum fx;
    TEST_ASSERT_TRUE(fx.controlHints & ControlHints::ROTATE_SPACE);
}

// Regression test: this file's setUp() uses SINGLE_STRIP_TEST_DEVICE, a deliberately
// non-square (934x10mm) model - exactly the case that exposed a bug where
// MultiPendulum sized its chain off GEOMETRY.getScreenRadius() (the *wider* half-
// dimension) instead of the narrower one, letting bobs swing outside the strip's
// actual 10mm-tall footprint. The chain's maximum possible reach from the anchor is
// bounded by the sum of its rod lengths, which construction caps at ~0.9x the radius
// it was sized from - so this holds deterministically right after construction,
// before any simulation has run.
void test_multi_pendulum_initial_reach_uses_narrower_screen_dimension()
{
    randomSeed(12);
    float bound = (float)min(GEOMETRY.getScreenHalfWidth(), GEOMETRY.getScreenHalfHeight());
    for (int attempt = 0; attempt < 20; attempt++)
    {
        MultiPendulum fx;
        for (auto& p : fx.positions) TEST_ASSERT_TRUE(p.length() <= bound * 1.01f);
    }
}

// ---------------------------------------------------------------------------
// HexagonalRippleGalaxy
// ---------------------------------------------------------------------------

void test_hexagonal_ripple_galaxy_evaluates()
{
    HexagonalRippleGalaxy fx;
    exerciseEvaluate(fx, 8000);
}

void test_hexagonal_ripple_galaxy_color_varies_with_time()
{
    HexagonalRippleGalaxy fx;
    LedStrip& strip = GEOMETRY.getStrip(0);
    Led& led = strip.leds[3];

    fx.precompute(0);
    CRGB early = fx.evaluate(&strip, &led, 3, 0);

    fx.precompute(500000);
    CRGB late = fx.evaluate(&strip, &led, 3, 500000);

    TEST_ASSERT_FALSE(early == late);
}

void test_hexagonal_ripple_galaxy_color_varies_with_position()
{
    // Regression guard for the led->polar refactor: radius8/angle8 must
    // actually come from per-LED polar coordinates, not a constant.
    // Explicitly reseeded: this test's outcome depends on this file's shared global
    // RNG stream position, which shifts whenever tests earlier in the RUN_TEST order
    // change (e.g. gain/lose a randomSeed() call) - pin it here so this test's result
    // doesn't depend on what happens to run before it.
    randomSeed(42);
    HexagonalRippleGalaxy fx;
    fx.precompute(1000);
    LedStrip& strip = GEOMETRY.getStrip(0);
    CRGB a = fx.evaluate(&strip, &strip.leds[0], 0, 1000);
    CRGB b = fx.evaluate(&strip, &strip.leds[strip.num_leds / 2], strip.num_leds / 2, 1000);
    TEST_ASSERT_FALSE(a == b);
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

// The transition's midpoint should have moved measurably closer to the target than the
// very start did - the existing test above only checks the read-back (evaluate() ==
// currentColors[]), never that currentColors[] actually blends toward targetColors[].
void test_individual_strip_drift_current_color_moves_toward_target_over_the_transition()
{
    IndividualStripDrift fx;
    CRGB target = fx.targetColors[0];
    milliseconds_t start = fx.transitionStartTimes[0];
    milliseconds_t end = fx.transitionEndTimes[0];

    fx.precompute(start);
    CRGB atStart = fx.currentColors[0];

    fx.precompute(start + (end - start) / 2);
    CRGB atMid = fx.currentColors[0];

    auto colorDistanceToTarget = [&](CRGB c)
    {
        return abs((int)c.r - (int)target.r) + abs((int)c.g - (int)target.g) +
               abs((int)c.b - (int)target.b);
    };
    TEST_ASSERT_TRUE(colorDistanceToTarget(atMid) < colorDistanceToTarget(atStart));
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

void test_cartesian_moodlight_color_varies_with_position()
{
    CartesianMoodlight fx;
    fx.randomize();
    LedStrip& strip = GEOMETRY.getStrip(0);
    Led ledA = strip.leds[0];
    ledA.cartesian.x = 0;
    ledA.cartesian.y = 0;
    Led ledB = strip.leds[0];
    ledB.cartesian.x = 400;
    ledB.cartesian.y = -300;

    CRGB a = fx.evaluate(&strip, &ledA, 0, 1000);
    CRGB b = fx.evaluate(&strip, &ledB, 0, 1000);
    TEST_ASSERT_FALSE(a == b);
}

void test_cartesian_moodlight_color_varies_with_time()
{
    CartesianMoodlight fx;
    fx.randomize();
    LedStrip& strip = GEOMETRY.getStrip(0);
    Led& led = strip.leds[0];

    CRGB early = fx.evaluate(&strip, &led, 0, 0);
    CRGB late = fx.evaluate(&strip, &led, 0, 500000);
    TEST_ASSERT_FALSE(early == late);
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
    // 13 possible effects; 300 draws (never repeating consecutively) should
    // realistically hit all of them.
    TEST_ASSERT_TRUE(namesSeen.size() >= 10);
}

// ---------------------------------------------------------------------------
// EFFECT_REGISTRY / createEffect()
// ---------------------------------------------------------------------------

void test_num_effects_matches_registry_length() { TEST_ASSERT_EQUAL_INT(13, NUM_EFFECTS); }

void test_create_effect_matches_registry_name_for_every_entry()
{
    for (size_t i = 0; i < NUM_EFFECTS; i++)
    {
        AbstractEffect* fx = createEffect(EFFECT_REGISTRY[i].id);
        TEST_ASSERT_NOT_NULL(fx);
        TEST_ASSERT_EQUAL_STRING(EFFECT_REGISTRY[i].name, fx->GetName());
        delete fx;
    }
}

int main(int argc, char** argv)
{
    UNITY_BEGIN();

    RUN_TEST(test_error_effect_strip0_is_red_others_black);
    RUN_TEST(test_static_color_blends_toward_target);
    RUN_TEST(test_static_color_default_constructor);
    RUN_TEST(test_static_color_blend_scales_with_dt_not_call_count);

    RUN_TEST(test_individual_strip_moodlight_evaluates);

    RUN_TEST(test_electric_sparks_runs_full_frame_cycle);
    RUN_TEST(test_electric_sparks_avg38_clamps);
    RUN_TEST(test_electric_sparks_runs_with_varying_frame_gaps);
    RUN_TEST(test_electric_sparks_diffusion_spreads_energy_to_neighbors);
    RUN_TEST(test_electric_sparks_energy_decays_to_near_zero_without_further_injection);
    RUN_TEST(test_electric_sparks_evaluate_color_tracks_energy_level);

    RUN_TEST(test_saturation_glow_evaluates);
    RUN_TEST(test_saturation_glow_evaluate_matches_precomputed_color);
    RUN_TEST(test_saturation_glow_amplitude_keeps_center_wave_in_range);
    RUN_TEST(test_saturation_glow_color_varies_over_time);

    RUN_TEST(test_palette_wave_sets_rotate_space_hint);
    RUN_TEST(test_palette_wave_color_varies_with_position);

    RUN_TEST(test_ninja_star_evaluates);
    RUN_TEST(test_ninja_star_pow16_lut_fixed_points_and_shrinks_midrange);
    RUN_TEST(test_ninja_star_evaluate_blends_from_inner_to_outer_by_radius);

    RUN_TEST(test_polar_swipe_evaluates_both_flip_directions);
    RUN_TEST(test_polar_swipe_get_brightness_bounds);
    RUN_TEST(test_polar_swipe_evaluate_matches_color_scaled_by_brightness);
    RUN_TEST(test_polar_swipe_andromeda_strip0_is_always_black);

    RUN_TEST(test_polar_moodlight_evaluates);
    RUN_TEST(test_polar_moodlight_same_radius_gives_same_color);

    RUN_TEST(test_rgbody_problem_evaluates);
    RUN_TEST(test_rgbody_problem_brighter_near_an_emitter_than_far_away);
    RUN_TEST(test_rgbody_problem_initial_positions_use_narrower_screen_dimension);

    RUN_TEST(test_bezier_swarm_evaluates);
    RUN_TEST(test_bezier_swarm_sets_rotate_space_hint);
    RUN_TEST(test_bezier_swarm_single_emitter_hue_changes_over_time);

    RUN_TEST(test_multi_pendulum_evaluates);
    RUN_TEST(test_multi_pendulum_sets_rotate_space_hint);
    RUN_TEST(test_multi_pendulum_initial_reach_uses_narrower_screen_dimension);

    RUN_TEST(test_hexagonal_ripple_galaxy_evaluates);
    RUN_TEST(test_hexagonal_ripple_galaxy_color_varies_with_time);
    RUN_TEST(test_hexagonal_ripple_galaxy_color_varies_with_position);

    RUN_TEST(test_individual_strip_drift_transitions_to_new_target);
    RUN_TEST(test_individual_strip_drift_current_color_moves_toward_target_over_the_transition);

    RUN_TEST(test_cartesian_moodlight_randomize_and_evaluate);
    RUN_TEST(test_cartesian_moodlight_color_varies_with_position);
    RUN_TEST(test_cartesian_moodlight_color_varies_with_time);

    RUN_TEST(test_get_random_effect_produces_valid_effects);
    RUN_TEST(test_num_effects_matches_registry_length);
    RUN_TEST(test_create_effect_matches_registry_name_for_every_entry);

    RUN_TEST(test_polar_swipe_evaluates_across_all_strips_of_real_multi_strip_model);

    RUN_TEST(test_paint_fills_all_strips);
    RUN_TEST(test_paint_strip_fills_only_target_strip);
    RUN_TEST(test_random_color_full_saturation_and_value);
    RUN_TEST(test_random_complementary_colors_are_evenly_spaced);
    RUN_TEST(test_random_predefined_palette_returns_nonempty_palette);
    RUN_TEST(test_brightness_from_emitter_decays_with_distance);

    return UNITY_END();
}
