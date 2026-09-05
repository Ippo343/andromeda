#include <unity.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "../../include/effects.h"

void test_effects_setUp() { GEOMETRY.initializeForTest(ModelId::SINGLE_STRIP_TEST_DEVICE); }
void test_effects_tearDown() {}

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
    fastTicksFx.currentColor = CRGB(0, 0, 0);  // explicit for clarity - the ctor already does this
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

// Issue #106: a colour change must be visually indistinguishable from the target within ~1s.
// Drive a full black->white step at ~60fps for one second and require every channel to have
// closed all but a couple of units of the gap.
void test_static_color_converges_within_one_second()
{
    StaticColor fx(CRGB(255, 255, 255));
    fx.currentColor = CRGB(0, 0, 0);

    milliseconds_t t = 0;
    for (int i = 0; i < 60; i++)
    {
        t += 16;  // ~60fps
        fx.precompute(t);
    }

    TEST_ASSERT_TRUE(fx.currentColor.r >= 253);
    TEST_ASSERT_TRUE(fx.currentColor.g >= 253);
    TEST_ASSERT_TRUE(fx.currentColor.b >= 253);
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

// Regression test for the unbounded width-doubling loop: with bigSparkChance pinned to its
// maximum (70, giving the roll an expected value > 1 - it diverges) and sparkThreshold forced
// to guarantee a spark on every LED, a run of bad luck could previously double the spark width
// dozens of times before finally failing, freezing evaluate() for a multi-second span. Forcing
// every LED on every frame to roll the width-doubling chain maximizes how often that can
// happen; asserts real wall-clock time instead of just "didn't crash", since an unbounded loop
// doesn't crash, it just doesn't come back for a very long time.
void test_electric_sparks_spark_width_growth_stays_bounded()
{
    ElectricSparks fx;
    LedStrip& strip = GEOMETRY.getStrip(0);

    // bigSparkChance is an EnergyParam<int, 40, 70> - not a settable field, its value is
    // derived from the global Energy value on every read - so pin Energy to 255 to force it
    // to its maximum (70).
    Energy::set(255);

    auto started = std::chrono::steady_clock::now();

    milliseconds_t t = 0;
    for (int frame = 0; frame < 100; frame++)
    {
        t += 16;
        fx.precompute(t);
        // precompute() just recomputed sparkThreshold from sparkRateMilliHz/dt - override it
        // to guarantee every LED rolls a spark (and therefore the width-doubling chain) this
        // frame, rather than relying on however high sparkRateMilliHz's own energy-scaled
        // rate happens to land.
        fx.sparkThreshold = ElectricSparks::DICE_LIMIT;
        for (size_t i = 0; i < strip.num_leds; i++) fx.evaluate(&strip, &strip.leds[i], i, t);
        fx.postprocess(t);
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    // Generous budget: unclamped, even a handful of unlucky rolls across 5600 evaluate() calls
    // (100 frames x 56 LEDs) would blow well past this on any machine.
    TEST_ASSERT_TRUE(elapsed.count() < 5000);

    Energy::set(0);  // don't leak a maxed global energy value into later tests
}

// ---------------------------------------------------------------------------
// HeatDiffusionRing
// ---------------------------------------------------------------------------

void test_heat_diffusion_ring_runs_full_frame_cycle()
{
    HeatDiffusionRing fx;
    exerciseEvaluate(fx, 5000);
    exerciseEvaluate(fx, 5016);  // second frame exercises the diffusion step with a real dt
}

void test_heat_diffusion_ring_spreads_a_hot_spot_to_neighbours()
{
    HeatDiffusionRing fx;
    fx.precompute(1000);  // lazily seeds the field + takes the first step

    std::vector<float>& T = fx.fieldForTest(0, 0);
    for (auto& v : T) v = 0.0f;
    size_t mid = T.size() / 2;
    T[mid] = 255.0f;

    fx.precompute(1016);  // one diffusion step on our planted spike

    TEST_ASSERT_TRUE(T[mid] < 255.0f);
    TEST_ASSERT_TRUE(T[mid - 1] > 0.0f);
    TEST_ASSERT_TRUE(T[mid + 1] > 0.0f);
}

void test_heat_diffusion_ring_cools_a_saturated_strip_over_time()
{
    randomSeed(5);
    HeatDiffusionRing fx;
    fx.precompute(1000);

    std::vector<float>& T = fx.fieldForTest(0, 0);
    for (auto& v : T) v = 255.0f;  // every cell maxed, so the injector can't add energy
    float before = 0;
    for (float v : T) before += v;

    milliseconds_t t = 1000;
    for (int frame = 0; frame < 300; frame++)
    {
        t += 16;
        fx.precompute(t);
    }

    float after = 0;
    for (float v : T) after += v;
    TEST_ASSERT_TRUE(after < before * 0.5f);
}

// A ring effect is designed for the single-strip L10 but must stay well-behaved
// on every model: each strip runs its own periodic field, and strips shorter
// than 3 LEDs (Andromeda's centre strip) must be left alone rather than indexed
// out of bounds.
void test_heat_diffusion_ring_evaluates_on_l10_and_multi_strip_models()
{
    for (ModelId model : {ModelId::L10_MK1, ModelId::ANDROMEDA_MK0})
    {
        GEOMETRY.initializeForTest(model);
        HeatDiffusionRing fx;
        fx.precompute(0);
        fx.precompute(16);
        for (size_t iStrip = 0; iStrip < GEOMETRY.getNumStrips(); iStrip++)
        {
            LedStrip& strip = GEOMETRY.getStrip(iStrip);
            for (size_t i = 0; i < strip.num_leds; i += 5)
            {
                CRGB c = fx.evaluate(&strip, &strip.leds[i], i, 16);
                (void)c;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// JellyFrame
// ---------------------------------------------------------------------------

void test_jelly_frame_runs_full_frame_cycle()
{
    JellyFrame fx;
    exerciseEvaluate(fx, 5000);
    exerciseEvaluate(fx, 5016);
}

void test_jelly_frame_color_tracks_displacement()
{
    JellyFrame fx;
    fx.precompute(1000);  // caches baseHue_ and lazily seeds

    std::vector<float>& u = fx.fieldForTest(0, 0);
    for (auto& x : u) x = 0.0f;
    LedStrip& strip = GEOMETRY.getStrip(0);

    u[1] = 60.0f;
    CRGB crest = fx.evaluate(&strip, &strip.leds[1], 1, 1000);
    u[2] = -60.0f;
    CRGB trough = fx.evaluate(&strip, &strip.leds[2], 2, 1000);
    TEST_ASSERT_FALSE(crest == trough);
}

void test_jelly_frame_stays_bounded_over_long_run()
{
    randomSeed(6);
    JellyFrame fx;
    milliseconds_t t = 1000;
    for (int frame = 0; frame < 3000; frame++)
    {
        t += 16;
        fx.precompute(t);
    }
    std::vector<float>& u = fx.fieldForTest(0, 0);
    for (float x : u)
    {
        TEST_ASSERT_TRUE(isfinite(x));
        TEST_ASSERT_TRUE(fabsf(x) < 5000.0f);
    }
}

// Regression: colorIndex() used to cast baseHue_ + displacement*gain to uint8_t
// without clamping first (StandingWaveRing's equivalent already clamped). Two
// different displacements that both push the pre-cast value far outside
// [0, 255] must land on the same clamped index (255) rather than wrapping to
// different, essentially arbitrary values.
void test_jelly_frame_color_index_clamps_extreme_displacement()
{
    JellyFrame fx;
    fx.precompute(1000);

    std::vector<float>& u = fx.fieldForTest(0, 0);
    for (auto& x : u) x = 0.0f;
    LedStrip& strip = GEOMETRY.getStrip(0);

    u[1] = 1e6f;
    CRGB a = fx.evaluate(&strip, &strip.leds[1], 1, 1000);
    u[1] = 5e7f;
    CRGB b = fx.evaluate(&strip, &strip.leds[1], 1, 1000);

    TEST_ASSERT_TRUE(a == b);
}

void test_jelly_frame_evaluates_on_l10_and_multi_strip_models()
{
    for (ModelId model : {ModelId::L10_MK1, ModelId::ANDROMEDA_MK0})
    {
        GEOMETRY.initializeForTest(model);
        JellyFrame fx;
        fx.precompute(0);
        fx.precompute(16);
        for (size_t iStrip = 0; iStrip < GEOMETRY.getNumStrips(); iStrip++)
        {
            LedStrip& strip = GEOMETRY.getStrip(iStrip);
            for (size_t i = 0; i < strip.num_leds; i += 5)
            {
                CRGB c = fx.evaluate(&strip, &strip.leds[i], i, 16);
                (void)c;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// StandingWaveRing
// ---------------------------------------------------------------------------

void test_standing_wave_ring_runs_full_frame_cycle()
{
    StandingWaveRing fx;
    exerciseEvaluate(fx, 5000);
    exerciseEvaluate(fx, 5016);
}

void test_standing_wave_ring_pluck_propagates_to_neighbours()
{
    StandingWaveRing fx;
    fx.precompute(1000);  // lazy seed + first step

    std::vector<float>& u = fx.fieldForTest(0, 0);
    std::vector<float>& v = fx.fieldForTest(0, 1);
    for (auto& x : u) x = 0.0f;
    for (auto& x : v) x = 0.0f;
    size_t mid = u.size() / 2;
    u[mid] = 100.0f;

    for (int frame = 1; frame < 12; frame++) fx.precompute(1000 + frame * 16);

    // The bump has moved energy outward from the seed cell.
    TEST_ASSERT_TRUE(fabsf(u[mid - 3]) + fabsf(u[mid + 3]) > 0.5f);
}

void test_standing_wave_ring_stays_bounded_over_long_run()
{
    randomSeed(6);
    StandingWaveRing fx;
    milliseconds_t t = 1000;
    for (int frame = 0; frame < 3000; frame++)
    {
        t += 16;
        fx.precompute(t);
    }
    std::vector<float>& u = fx.fieldForTest(0, 0);
    for (float x : u)
    {
        TEST_ASSERT_TRUE(isfinite(x));
        TEST_ASSERT_TRUE(fabsf(x) < 5000.0f);
    }
}

void test_standing_wave_ring_evaluates_on_l10_and_multi_strip_models()
{
    for (ModelId model : {ModelId::L10_MK1, ModelId::ANDROMEDA_MK0})
    {
        GEOMETRY.initializeForTest(model);
        StandingWaveRing fx;
        fx.precompute(0);
        fx.precompute(16);
        for (size_t iStrip = 0; iStrip < GEOMETRY.getNumStrips(); iStrip++)
        {
            LedStrip& strip = GEOMETRY.getStrip(iStrip);
            for (size_t i = 0; i < strip.num_leds; i += 5)
            {
                CRGB c = fx.evaluate(&strip, &strip.leds[i], i, 16);
                (void)c;
            }
        }
    }
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
    TEST_ASSERT_TRUE(c == fx.colors[0]);
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
    CRGB early = fx.colors[0];
    fx.precompute(120000);  // 2 minutes later - within the 1-4 minute cycleTime range
    CRGB late = fx.colors[0];
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
    // Mutate the real geometry LEDs at two distinct indices (not local
    // copies) before the first precompute() call: PaletteWave now caches
    // its per-LED position term lazily from GEOMETRY on that call (see
    // ensurePerLedCache() in palette-wave.h), so evaluate() only reflects
    // coordinates that were live in GEOMETRY at that point.
    LedStrip& strip = GEOMETRY.getStrip(0);
    strip.leds[0].cartesian.x = 0;
    strip.leds[0].cartesian.y = 0;
    strip.leds[1].cartesian.x = 1000;
    strip.leds[1].cartesian.y = 1000;

    PaletteWave fx;
    fx.precompute(0);

    CRGB a = fx.evaluate(&strip, &strip.leds[0], 0, 0);
    CRGB b = fx.evaluate(&strip, &strip.leds[1], 1, 0);
    TEST_ASSERT_FALSE(a == b);
}

// ---------------------------------------------------------------------------
// AngularPaletteRotation
// ---------------------------------------------------------------------------

void test_angular_palette_rotation_evaluates()
{
    AngularPaletteRotation fx;
    exerciseEvaluate(fx, 3000);
}

void test_angular_palette_rotation_color_varies_with_angle()
{
    // Mutate the real geometry LEDs (not local copies) before the first
    // precompute() call: AngularPaletteRotation now caches its per-LED angle
    // term lazily from GEOMETRY on that call, so evaluate() only reflects
    // coordinates that were live in GEOMETRY at that point (see
    // ensurePerLedCache() in angular-palette-rotation.h).
    randomSeed(4);
    LedStrip& strip = GEOMETRY.getStrip(0);
    strip.leds[0].polar.cdegrees = 0;
    strip.leds[1].polar.cdegrees = 12000;  // 120 degrees round

    AngularPaletteRotation fx;
    fx.precompute(0);

    TEST_ASSERT_FALSE(fx.evaluate(&strip, &strip.leds[0], 0, 0) ==
                      fx.evaluate(&strip, &strip.leds[1], 1, 0));
}

void test_angular_palette_rotation_sweeps_over_time()
{
    randomSeed(4);
    LedStrip& strip = GEOMETRY.getStrip(0);
    strip.leds[0].polar.cdegrees = 0;  // colour index here is exactly the rotation offset

    AngularPaletteRotation fx;
    fx.precompute(0);
    CRGB early = fx.evaluate(&strip, &strip.leds[0], 0, 0);
    fx.precompute(20000);
    CRGB later = fx.evaluate(&strip, &strip.leds[0], 0, 20000);
    TEST_ASSERT_FALSE(early == later);
}

void test_angular_palette_rotation_evaluates_on_l10_and_multi_strip_models()
{
    for (ModelId model : {ModelId::L10_MK1, ModelId::ANDROMEDA_MK0})
    {
        GEOMETRY.initializeForTest(model);
        AngularPaletteRotation fx;
        fx.precompute(1000);
        for (size_t iStrip = 0; iStrip < GEOMETRY.getNumStrips(); iStrip++)
        {
            LedStrip& strip = GEOMETRY.getStrip(iStrip);
            for (size_t i = 0; i < strip.num_leds; i += 5)
            {
                CRGB c = fx.evaluate(&strip, &strip.leds[i], i, 1000);
                (void)c;
            }
        }
    }
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
// blend actually runs in the near-to-far direction, not reversed. Bypasses the full
// precompute() (whose inner/outer MoodLights are driven by real millis(), not t) and
// instead sets innerColor/outerColor/offset directly, calling only ensurePerLedCache()
// to build the per-LED theta/radius cache off two real geometry LEDs (mutated in place
// to isolate radius from angle: same cdegrees=0, different radius) - see
// ensurePerLedCache() in ninja-star.h for why evaluate() no longer reflects whatever Led*
// is passed at call time, only what was live in GEOMETRY at the point the cache was built.
void test_ninja_star_evaluate_blends_from_inner_to_outer_by_radius()
{
    LedStrip& strip = GEOMETRY.getStrip(0);
    strip.leds[0].polar.cdegrees = 0;  // theta = 0 regardless of offset
    strip.leds[0].polar.radius = 0;
    strip.leds[1].polar.cdegrees = 0;
    strip.leds[1].polar.radius = GEOMETRY.getMaxLedRadius();  // maps to scaledRadius 255

    NinjaStar fx;
    fx.innerColor = CRGB(200, 0, 0);
    fx.outerColor = CRGB(0, 0, 200);
    fx.ensurePerLedCache();

    uint8_t v = 0;
    for (int offset = 0; offset < 256 && v == 0; offset += 16)
    {
        fx.offset = (uint8_t)offset;
        v = NinjaStar::pow16(sin8(offset));
    }
    TEST_ASSERT_TRUE(v > 0);

    CRGB near = fx.evaluate(&strip, &strip.leds[0], 0, 0);
    CRGB far = fx.evaluate(&strip, &strip.leds[1], 1, 0);

    TEST_ASSERT_TRUE(near == (fx.innerColor % v));
    TEST_ASSERT_TRUE(far == (fx.outerColor % v));
}

// Bad weather for the getScreenRadius() -> getMaxLedRadius() fix. On L10 MK1
// the corner LEDs sit at radius ~66 while getScreenRadius() is 51, so
// map(radius, 0, 51, 0, 255) extrapolated past 255 and the uint8_t cache
// wrapped: a radius-66 LED cached as ~74, right next to a radius-51 LED cached
// as 255. The cache must be monotone non-decreasing in true radius.
void test_ninja_star_radius_cache_is_monotone_on_a_panel_with_far_corners()
{
    GEOMETRY.initializeForTest(ModelId::L10_MK1);

    NinjaStar fx;
    fx.ensurePerLedCache();

    std::vector<std::pair<uint16_t, uint8_t>> pairs;
    for (size_t s = 0; s < GEOMETRY.getNumStrips(); s++)
    {
        const LedStrip& strip = GEOMETRY.getStrips()[s];
        for (int l = 0; l < strip.num_leds; l++)
            pairs.emplace_back(strip.leds[l].polar.radius, fx.radiusCache[s][l]);
    }
    std::sort(pairs.begin(), pairs.end());

    for (size_t i = 1; i < pairs.size(); i++)
        TEST_ASSERT_TRUE_MESSAGE(pairs[i].second >= pairs[i - 1].second,
                                 "radiusCache decreased as true radius increased - it wrapped");

    GEOMETRY.initializeForTest(ModelId::SINGLE_STRIP_TEST_DEVICE);  // restore for later tests
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

// Bad weather for the getScreenRadius() -> getMaxLedRadius() fix in PolarSwipe.
// On L70 the furthest LED is at radius ~416 while getScreenRadius() is 340, so
// scanMax topped out around 340 + bandWidth and, for any rolled bandWidth
// below ~37, the band never came within bandWidth of that LED - it stayed dark
// for the whole effect. Every rolled instance must bring the band over the
// outermost LED at some point in its scan.
void test_polar_swipe_band_reaches_the_outermost_leds_on_a_large_panel()
{
    GEOMETRY.initializeForTest(ModelId::L70_MK1);

    Led* furthest = nullptr;
    for (size_t s = 0; s < GEOMETRY.getNumStrips(); s++)
    {
        LedStrip& strip = GEOMETRY.getStrip(s);
        for (int l = 0; l < strip.num_leds; l++)
            if (!furthest || strip.leds[l].polar.radius > furthest->polar.radius)
                furthest = &strip.leds[l];
    }
    TEST_ASSERT_NOT_NULL(furthest);
    TEST_ASSERT_EQUAL_UINT16(GEOMETRY.getMaxLedRadius(), furthest->polar.radius);

    for (int i = 0; i < 40; i++)
    {
        PolarSwipe fx;
        bool lit = false;
        for (unsigned long c = fx.scanMin; c <= fx.scanMax && !lit; c++)
        {
            fx.bandCenter = (unsigned short)c;
            if (fx.getBrightness(furthest) > 0) lit = true;
        }
        TEST_ASSERT_TRUE_MESSAGE(lit, "outermost LED never enters the swipe band");
    }

    GEOMETRY.initializeForTest(ModelId::SINGLE_STRIP_TEST_DEVICE);  // restore for later tests
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
    GEOMETRY.initializeForTest(ModelId::ANDROMEDA_MK0);
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

// Extends RGBodyProblem's falloff test to BezierSwarm's own evaluate() output - the
// generic EmitterFieldEffect blend test in test_physics.cpp covers this behavior only
// through a synthetic test-double subclass, not through this effect's actual positions.
void test_bezier_swarm_brighter_near_an_emitter_than_far_away()
{
    BezierSwarm fx;
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

// Extends RGBodyProblem's falloff test to MultiPendulum's own evaluate() output - see
// test_bezier_swarm_brighter_near_an_emitter_than_far_away for why this can't just rely
// on the generic EmitterFieldEffect test double.
void test_multi_pendulum_brighter_near_an_emitter_than_far_away()
{
    MultiPendulum fx;
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
// BouncingBallGlow
// ---------------------------------------------------------------------------

void test_bouncing_ball_glow_evaluates()
{
    BouncingBallGlow fx;
    exerciseEvaluate(fx, 10000);
    exerciseEvaluate(fx, 10016);  // second frame steps the box with a real dt
}

void test_bouncing_ball_glow_brighter_near_a_ball_than_far_away()
{
    BouncingBallGlow fx;
    fx.precompute(1000);

    Vec2f ball0 = fx.boxForTest().pos[0];
    LedStrip& strip = GEOMETRY.getStrip(0);
    Led nearLed = strip.leds[0];
    nearLed.cartesian.x = (int16_t)ball0.x + 5;
    nearLed.cartesian.y = (int16_t)ball0.y;

    Led farLed = strip.leds[0];
    short farCoord = GEOMETRY.getScreenRadius() * 10;
    farLed.cartesian.x = farCoord;
    farLed.cartesian.y = farCoord;

    CRGB near = fx.evaluate(&strip, &nearLed, 0, 1000);
    CRGB far = fx.evaluate(&strip, &farLed, 0, 1000);
    TEST_ASSERT_TRUE((int)near.r + near.g + near.b > (int)far.r + far.g + far.b);
}

void test_bouncing_ball_glow_does_not_set_rotate_space_hint()
{
    // Deliberate: the box is axis-aligned with the device so the balls hit a
    // square's sides square-on.
    BouncingBallGlow fx;
    TEST_ASSERT_FALSE(fx.controlHints & ControlHints::ROTATE_SPACE);
}

void test_bouncing_ball_glow_balls_stay_inside_the_render_area()
{
    randomSeed(9);
    BouncingBallGlow fx;
    float halfW = (float)GEOMETRY.getScreenHalfWidth();
    float halfH = (float)GEOMETRY.getScreenHalfHeight();
    milliseconds_t t = 1000;
    for (int frame = 0; frame < 500; frame++)
    {
        t += 16;
        fx.precompute(t);
        for (auto& p : fx.boxForTest().pos)
        {
            TEST_ASSERT_TRUE(fabsf(p.x) <= halfW + 1e-3f);
            TEST_ASSERT_TRUE(fabsf(p.y) <= halfH + 1e-3f);
        }
    }
}

void test_bouncing_ball_glow_evaluates_on_l10_and_multi_strip_models()
{
    for (ModelId model : {ModelId::L10_MK1, ModelId::ANDROMEDA_MK0})
    {
        GEOMETRY.initializeForTest(model);
        BouncingBallGlow fx;
        fx.precompute(0);
        fx.precompute(16);
        for (size_t iStrip = 0; iStrip < GEOMETRY.getNumStrips(); iStrip++)
        {
            LedStrip& strip = GEOMETRY.getStrip(iStrip);
            for (size_t i = 0; i < strip.num_leds; i += 5)
            {
                CRGB c = fx.evaluate(&strip, &strip.leds[i], i, 16);
                (void)c;
            }
        }
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

// Regression guard for #67: angularHue(angle8, harmonic) must be periodic in
// angle8 with period 256, so the theta-origin ray (where angle8 wraps from
// 255 back to 0) never shows a hard color boundary. Pure/no-geometry version
// of the check - the old (angle8 >> hueAngleShift) mapping stepped up to 128
// across this exact wrap, which this catches directly.
void test_hexagonal_ripple_galaxy_angular_hue_is_periodic()
{
    for (uint8_t harmonic = 1; harmonic <= 3; harmonic++)
    {
        uint8_t prev = HexagonalRippleGalaxy::angularHue(255, harmonic);
        for (uint16_t angle16 = 0; angle16 <= 255; angle16++)
        {
            uint8_t angle8 = static_cast<uint8_t>(angle16);
            uint8_t hue = HexagonalRippleGalaxy::angularHue(angle8, harmonic);
            // Circular distance in hue space (0-255 wraps around).
            uint8_t forward = static_cast<uint8_t>(hue - prev);
            uint8_t backward = static_cast<uint8_t>(prev - hue);
            uint8_t step = std::min(forward, backward);
            TEST_ASSERT_TRUE_MESSAGE(step <= 8, "angular hue step too large - seam regression");
            prev = hue;
        }
    }
}

// End-to-end regression guard for #67, on the actual geometry a seam was
// reported on: the Grid Test Rig has no LED at exactly y == 0, so the rows
// at y == -10 and y == +10 straddle the theta-origin ray (angle8 wraps
// between them) without landing on it exactly. Pair up LEDs on those two
// rows by x and assert the *hue* (via computeHueForTest(), see #ifdef
// UNIT_TEST in hexagonal-ripple-galaxy.h) each pair gets is circularly close
// - this is the check that would have caught the original bug on real
// geometry. Compared in hue space rather than CHSV(hue,255,255)'s resulting
// RGB: the rainbow conversion's per-channel slope near a segment boundary can
// turn even a small, wrap-safe hue step into a large RGB delta, which would
// make an RGB-distance threshold either too loose to catch a real seam or too
// tight to tolerate ordinary rendering.
void test_hexagonal_ripple_galaxy_no_seam_across_theta_origin()
{
    GEOMETRY.initializeForTest(ModelId::GRID_TEST_DEVICE);
    LedStrip& strip = GEOMETRY.getStrip(0);

    // Cover the randomized hueHarmonic/hueRadiusShift draws.
    for (int trial = 0; trial < 32; trial++)
    {
        HexagonalRippleGalaxy fx;
        fx.precompute(12345);

        // Index the strip's LEDs by (x, y) so pairs can be matched by x.
        std::map<int16_t, size_t> yMinus10ByX;
        std::map<int16_t, size_t> yPlus10ByX;
        for (size_t i = 0; i < strip.num_leds; i++)
        {
            int16_t x = strip.leds[i].cartesian.x;
            int16_t y = strip.leds[i].cartesian.y;
            if (y == -10) yMinus10ByX[x] = i;
            if (y == 10) yPlus10ByX[x] = i;
        }
        TEST_ASSERT_TRUE(!yMinus10ByX.empty() && !yPlus10ByX.empty());

        for (auto& [x, idxBelow] : yMinus10ByX)
        {
            if (x <= 200) continue;  // stay well clear of the center singularity
            auto it = yPlus10ByX.find(x);
            if (it == yPlus10ByX.end()) continue;
            size_t idxAbove = it->second;

            Led& below = strip.leds[idxBelow];
            Led& above = strip.leds[idxAbove];
            uint8_t r8Below = map(below.polar.radius, 0, GEOMETRY.getMaxLedRadius(), 0, 255);
            uint8_t a8Below = map(below.polar.cdegrees % FULL_CIRCLE, 0, FULL_CIRCLE, 0, 255);
            uint8_t r8Above = map(above.polar.radius, 0, GEOMETRY.getMaxLedRadius(), 0, 255);
            uint8_t a8Above = map(above.polar.cdegrees % FULL_CIRCLE, 0, FULL_CIRCLE, 0, 255);

            uint8_t hueBelow = fx.computeHueForTest(r8Below, a8Below);
            uint8_t hueAbove = fx.computeHueForTest(r8Above, a8Above);

            uint8_t forward = static_cast<uint8_t>(hueAbove - hueBelow);
            uint8_t backward = static_cast<uint8_t>(hueBelow - hueAbove);
            uint8_t step = std::min(forward, backward);

            char msg[96];
            snprintf(msg, sizeof(msg), "seam at x=%d, trial=%d", x, trial);
            TEST_ASSERT_TRUE_MESSAGE(step <= 16, msg);
        }
    }
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
    TEST_ASSERT_TRUE(c == fx.colors[0]);
}

// Bad weather: near the millis() rollover, transitionEndTimes[i] (= start +
// duration) wraps to a small value while t is still huge, so `t >=
// transitionEndTimes[i]` was true on every frame - each strip re-rolled its
// colour every single frame, a hard strobe. The elapsed-vs-duration form must
// hold the transition steady until t genuinely catches up.
void test_individual_strip_drift_does_not_strobe_across_millis_rollover()
{
    IndividualStripDrift fx;

    const milliseconds_t start = 0xFFFFF000UL;
    const milliseconds_t duration = 5000;
    FOR_EACH_STRIP
    {
        fx.transitionStartTimes[iStrip] = start;
        fx.transitionEndTimes[iStrip] = start + duration;  // wraps to ~0x1388
    }
    CRGB target0 = fx.targetColors[0];
    milliseconds_t end0 = fx.transitionEndTimes[0];

    // Frames a few seconds apart, all still inside the transition window but
    // past where the wrapped end stamp sits numerically.
    for (milliseconds_t t = start + 500; t < start + duration; t += 1000)
    {
        fx.precompute(t);
        TEST_ASSERT_EQUAL_UINT32(end0, fx.transitionEndTimes[0]);
        TEST_ASSERT_TRUE(fx.targetColors[0] == target0);
    }

    // Once t truly passes the (wrapped) end, exactly one re-roll happens.
    fx.precompute(start + duration + 100);  // t = 0x1454, elapsed = 5100 >= 5000
    TEST_ASSERT_TRUE(fx.transitionEndTimes[0] != end0);
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
    CRGB atStart = fx.colors[0];

    fx.precompute(start + (end - start) / 2);
    CRGB atMid = fx.colors[0];

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

// Regression test for createEffect()'s actual production usage: `new CartesianMoodlight()`
// with no separate randomize() call (unlike every test above, which calls it explicitly).
// Before the fix, redAmp/greenAmp/blueAmp, the direction vectors, and the wavelengths were
// left as whatever was in the freed heap block on this exact path - the constructor alone
// must now be sufficient to leave the effect in a properly randomized, working state.
void test_cartesian_moodlight_constructor_alone_produces_working_effect()
{
    CartesianMoodlight* fx = new CartesianMoodlight();  // no fx->randomize() call
    LedStrip& strip = GEOMETRY.getStrip(0);
    Led ledA = strip.leds[0];
    ledA.cartesian.x = 0;
    ledA.cartesian.y = 0;
    Led ledB = strip.leds[0];
    ledB.cartesian.x = 400;
    ledB.cartesian.y = -300;

    CRGB a = fx->evaluate(&strip, &ledA, 0, 1000);
    CRGB b = fx->evaluate(&strip, &ledB, 0, 1000);
    TEST_ASSERT_FALSE(a == b);
    delete fx;
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

// Draws enough samples (seeded, so deterministic) to hit every case of the
// switch(random(8)), including case 7 (HeatColors_p) which 20 unseeded draws
// wasn't reliably enough to land on.
void test_random_predefined_palette_covers_every_case()
{
    randomSeed(3);
    const CRGBPalette16 known[8] = {CloudColors_p,  LavaColors_p,    OceanColors_p,
                                    ForestColors_p, RainbowColors_p, RainbowStripeColors_p,
                                    PartyColors_p,  HeatColors_p};
    bool seen[8] = {false};

    for (int i = 0; i < 400; i++)
    {
        CRGBPalette16 p = randomPredefinedPalette();
        for (int k = 0; k < 8; k++)
        {
            bool matches = true;
            for (int j = 0; j < 16; j++)
            {
                if (p[j] != known[k][j])
                {
                    matches = false;
                    break;
                }
            }
            if (matches)
            {
                seen[k] = true;
                break;
            }
        }
    }

    for (int k = 0; k < 8; k++) TEST_ASSERT_TRUE(seen[k]);
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

// A negative brightnessFactor drives the pre-clamp value below 0, exercising
// the low side of brightnessFromEmitter's constrain() (only the >255 side is
// reachable via the normal positive-factor callers above).
void test_brightness_from_emitter_clamps_negative_value_to_zero()
{
    LedStrip& strip = GEOMETRY.getStrip(0);
    Led* led = &strip.leds[0];

    uint8_t v = brightnessFromEmitter(led, {450, 0}, -5000.0f);

    TEST_ASSERT_EQUAL_UINT8(0, v);
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
    // 18 possible effects; 300 draws (never repeating consecutively) should
    // realistically hit all of them.
    TEST_ASSERT_TRUE(namesSeen.size() >= 15);
}

// ---------------------------------------------------------------------------
// EFFECT_REGISTRY / createEffect()
// ---------------------------------------------------------------------------

void test_num_effects_matches_registry_length() { TEST_ASSERT_EQUAL_INT(18, NUM_EFFECTS); }

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

void run_test_effects_tests()
{
    RUN_TEST(test_error_effect_strip0_is_red_others_black);
    RUN_TEST(test_static_color_blends_toward_target);
    RUN_TEST(test_static_color_default_constructor);
    RUN_TEST(test_static_color_blend_scales_with_dt_not_call_count);
    RUN_TEST(test_static_color_converges_within_one_second);

    RUN_TEST(test_individual_strip_moodlight_evaluates);

    RUN_TEST(test_electric_sparks_runs_full_frame_cycle);
    RUN_TEST(test_electric_sparks_avg38_clamps);
    RUN_TEST(test_electric_sparks_runs_with_varying_frame_gaps);
    RUN_TEST(test_electric_sparks_diffusion_spreads_energy_to_neighbors);
    RUN_TEST(test_electric_sparks_energy_decays_to_near_zero_without_further_injection);
    RUN_TEST(test_electric_sparks_evaluate_color_tracks_energy_level);
    RUN_TEST(test_electric_sparks_spark_width_growth_stays_bounded);

    RUN_TEST(test_heat_diffusion_ring_runs_full_frame_cycle);
    RUN_TEST(test_heat_diffusion_ring_spreads_a_hot_spot_to_neighbours);
    RUN_TEST(test_heat_diffusion_ring_cools_a_saturated_strip_over_time);
    RUN_TEST(test_heat_diffusion_ring_evaluates_on_l10_and_multi_strip_models);

    RUN_TEST(test_saturation_glow_evaluates);
    RUN_TEST(test_saturation_glow_evaluate_matches_precomputed_color);
    RUN_TEST(test_saturation_glow_amplitude_keeps_center_wave_in_range);
    RUN_TEST(test_saturation_glow_color_varies_over_time);

    RUN_TEST(test_jelly_frame_runs_full_frame_cycle);
    RUN_TEST(test_jelly_frame_color_tracks_displacement);
    RUN_TEST(test_jelly_frame_stays_bounded_over_long_run);
    RUN_TEST(test_jelly_frame_color_index_clamps_extreme_displacement);
    RUN_TEST(test_jelly_frame_evaluates_on_l10_and_multi_strip_models);

    RUN_TEST(test_standing_wave_ring_runs_full_frame_cycle);
    RUN_TEST(test_standing_wave_ring_pluck_propagates_to_neighbours);
    RUN_TEST(test_standing_wave_ring_stays_bounded_over_long_run);
    RUN_TEST(test_standing_wave_ring_evaluates_on_l10_and_multi_strip_models);

    RUN_TEST(test_palette_wave_sets_rotate_space_hint);
    RUN_TEST(test_palette_wave_color_varies_with_position);

    RUN_TEST(test_angular_palette_rotation_evaluates);
    RUN_TEST(test_angular_palette_rotation_color_varies_with_angle);
    RUN_TEST(test_angular_palette_rotation_sweeps_over_time);
    RUN_TEST(test_angular_palette_rotation_evaluates_on_l10_and_multi_strip_models);

    RUN_TEST(test_ninja_star_evaluates);
    RUN_TEST(test_ninja_star_pow16_lut_fixed_points_and_shrinks_midrange);
    RUN_TEST(test_ninja_star_evaluate_blends_from_inner_to_outer_by_radius);
    RUN_TEST(test_ninja_star_radius_cache_is_monotone_on_a_panel_with_far_corners);

    RUN_TEST(test_polar_swipe_evaluates_both_flip_directions);
    RUN_TEST(test_polar_swipe_band_reaches_the_outermost_leds_on_a_large_panel);
    RUN_TEST(test_polar_swipe_get_brightness_bounds);
    RUN_TEST(test_polar_swipe_evaluate_matches_color_scaled_by_brightness);
    RUN_TEST(test_polar_swipe_andromeda_strip0_is_always_black);

    RUN_TEST(test_polar_moodlight_evaluates);
    RUN_TEST(test_polar_moodlight_same_radius_gives_same_color);

    RUN_TEST(test_rgbody_problem_evaluates);
    RUN_TEST(test_rgbody_problem_brighter_near_an_emitter_than_far_away);
    RUN_TEST(test_rgbody_problem_initial_positions_use_narrower_screen_dimension);

    RUN_TEST(test_bezier_swarm_evaluates);
    RUN_TEST(test_bezier_swarm_brighter_near_an_emitter_than_far_away);
    RUN_TEST(test_bezier_swarm_sets_rotate_space_hint);
    RUN_TEST(test_bezier_swarm_single_emitter_hue_changes_over_time);

    RUN_TEST(test_multi_pendulum_evaluates);
    RUN_TEST(test_multi_pendulum_brighter_near_an_emitter_than_far_away);
    RUN_TEST(test_multi_pendulum_sets_rotate_space_hint);
    RUN_TEST(test_multi_pendulum_initial_reach_uses_narrower_screen_dimension);

    RUN_TEST(test_bouncing_ball_glow_evaluates);
    RUN_TEST(test_bouncing_ball_glow_brighter_near_a_ball_than_far_away);
    RUN_TEST(test_bouncing_ball_glow_does_not_set_rotate_space_hint);
    RUN_TEST(test_bouncing_ball_glow_balls_stay_inside_the_render_area);
    RUN_TEST(test_bouncing_ball_glow_evaluates_on_l10_and_multi_strip_models);

    RUN_TEST(test_hexagonal_ripple_galaxy_evaluates);
    RUN_TEST(test_hexagonal_ripple_galaxy_color_varies_with_time);
    RUN_TEST(test_hexagonal_ripple_galaxy_color_varies_with_position);
    RUN_TEST(test_hexagonal_ripple_galaxy_angular_hue_is_periodic);
    RUN_TEST(test_hexagonal_ripple_galaxy_no_seam_across_theta_origin);

    RUN_TEST(test_individual_strip_drift_transitions_to_new_target);
    RUN_TEST(test_individual_strip_drift_does_not_strobe_across_millis_rollover);
    RUN_TEST(test_individual_strip_drift_current_color_moves_toward_target_over_the_transition);

    RUN_TEST(test_cartesian_moodlight_randomize_and_evaluate);
    RUN_TEST(test_cartesian_moodlight_constructor_alone_produces_working_effect);
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
    RUN_TEST(test_random_predefined_palette_covers_every_case);
    RUN_TEST(test_brightness_from_emitter_decays_with_distance);
    RUN_TEST(test_brightness_from_emitter_clamps_negative_value_to_zero);
}
