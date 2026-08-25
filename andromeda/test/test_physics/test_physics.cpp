#include <unity.h>

#include <algorithm>
#include <cmath>

#include "effects/emitter-field-effect.h"
#include "geometry/geometry.h"
#include "physics/bezier-path.h"
#include "physics/frame-clock.h"
#include "physics/physics-random.h"
#include "physics/vec2f.h"

// Realistic square-ish model, unlike the degenerate 10x934mm strip test_effects.cpp
// uses - needed here because bounds-containment tests (added in later commits) care
// about a sane aspect ratio.
void setUp() { GEOMETRY.initializeForTest(ModelId::ANDROMEDA_MK1); }
void tearDown() {}

// ---------------------------------------------------------------------------
// Vec2f
// ---------------------------------------------------------------------------

void test_vec2f_addition()
{
    Vec2f a(1, 2), b(3, 4);
    Vec2f c = a + b;
    TEST_ASSERT_EQUAL_FLOAT(4, c.x);
    TEST_ASSERT_EQUAL_FLOAT(6, c.y);
}

void test_vec2f_subtraction()
{
    Vec2f a(5, 7), b(2, 3);
    Vec2f c = a - b;
    TEST_ASSERT_EQUAL_FLOAT(3, c.x);
    TEST_ASSERT_EQUAL_FLOAT(4, c.y);
}

void test_vec2f_scalar_multiply_both_orders()
{
    Vec2f a(2, -3);
    Vec2f b = a * 2.0f;
    Vec2f c = 2.0f * a;
    TEST_ASSERT_EQUAL_FLOAT(4, b.x);
    TEST_ASSERT_EQUAL_FLOAT(-6, b.y);
    TEST_ASSERT_EQUAL_FLOAT(b.x, c.x);
    TEST_ASSERT_EQUAL_FLOAT(b.y, c.y);
}

void test_vec2f_compound_assignment()
{
    Vec2f a(1, 1);
    a += Vec2f(2, 3);
    TEST_ASSERT_EQUAL_FLOAT(3, a.x);
    TEST_ASSERT_EQUAL_FLOAT(4, a.y);
    a -= Vec2f(1, 1);
    TEST_ASSERT_EQUAL_FLOAT(2, a.x);
    TEST_ASSERT_EQUAL_FLOAT(3, a.y);
}

void test_vec2f_dot_product()
{
    Vec2f a(1, 2), b(3, 4);
    TEST_ASSERT_EQUAL_FLOAT(11.0f, a.dot(b));
}

void test_vec2f_length_and_length_squared()
{
    Vec2f a(3, 4);
    TEST_ASSERT_EQUAL_FLOAT(25.0f, a.lengthSquared());
    TEST_ASSERT_EQUAL_FLOAT(5.0f, a.length());
}

void test_vec2f_normalized_unit_vector()
{
    Vec2f a(3, 4);
    Vec2f n = a.normalized();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, n.length());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.6f, n.x);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.8f, n.y);
}

void test_vec2f_normalized_zero_vector_is_zero_not_nan()
{
    Vec2f zero(0, 0);
    Vec2f n = zero.normalized();
    TEST_ASSERT_FALSE(std::isnan(n.x));
    TEST_ASSERT_FALSE(std::isnan(n.y));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, n.x);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, n.y);
}

void test_vec2f_cartesian_round_trip()
{
    CartesianCoordinates c;
    c.x = 123;
    c.y = -456;
    Vec2f v = Vec2f::fromCartesian(c);
    CartesianCoordinates back = v.toCartesian();
    TEST_ASSERT_EQUAL_INT16(c.x, back.x);
    TEST_ASSERT_EQUAL_INT16(c.y, back.y);
}

void test_vec2f_to_cartesian_rounds_to_nearest()
{
    Vec2f v(2.6f, -2.6f);
    CartesianCoordinates c = v.toCartesian();
    TEST_ASSERT_EQUAL_INT16(3, c.x);
    TEST_ASSERT_EQUAL_INT16(-3, c.y);
}

// ---------------------------------------------------------------------------
// randomFloat
// ---------------------------------------------------------------------------

void test_random_float_stays_within_range()
{
    randomSeed(42);
    for (int i = 0; i < 1000; i++)
    {
        float v = randomFloat(-5.0f, 10.0f);
        TEST_ASSERT_TRUE(v >= -5.0f);
        TEST_ASSERT_TRUE(v < 10.0f);
    }
}

void test_random_float_covers_both_ends_of_range()
{
    randomSeed(7);
    float minSeen = 1e9f, maxSeen = -1e9f;
    for (int i = 0; i < 2000; i++)
    {
        float v = randomFloat(0.0f, 1.0f);
        minSeen = std::min(minSeen, v);
        maxSeen = std::max(maxSeen, v);
    }
    TEST_ASSERT_TRUE(minSeen < 0.05f);
    TEST_ASSERT_TRUE(maxSeen > 0.95f);
}

// ---------------------------------------------------------------------------
// FrameClock
// ---------------------------------------------------------------------------

void test_frame_clock_first_tick_uses_16ms_fallback()
{
    FrameClock clock;
    milliseconds_t dt = clock.tick(5000);
    TEST_ASSERT_EQUAL_UINT32(16, dt);
}

void test_frame_clock_reports_real_delta_on_subsequent_ticks()
{
    FrameClock clock;
    clock.tick(5000);
    milliseconds_t dt = clock.tick(5033);
    TEST_ASSERT_EQUAL_UINT32(33, dt);
}

void test_frame_clock_clamps_huge_gap_to_max_dt()
{
    FrameClock clock;
    clock.tick(1000);
    milliseconds_t dt = clock.tick(50000, 250);
    TEST_ASSERT_EQUAL_UINT32(250, dt);
}

// ---------------------------------------------------------------------------
// EmitterFieldEffect
// ---------------------------------------------------------------------------

// Minimal no-op subclass: positions/colors are set directly by the test, no real
// physics, just proving the shared additive-blend rendering math in isolation.
class NoOpEmitterField : public EmitterFieldEffect
{
   public:
    explicit NoOpEmitterField(size_t n) : EmitterFieldEffect(n) {}
    const char* GetName() override { return "NoOpEmitterField"; }
    void updatePositions(milliseconds_t, milliseconds_t) override {}
};

void test_emitter_field_effect_blends_colors_additively_at_zero_distance()
{
    NoOpEmitterField fx(2);
    fx.positions[0] = Vec2f(0, 0);
    fx.positions[1] = Vec2f(0, 0);
    fx.colors[0] = CHSV(0, 0, 255);  // full-value white-ish -> CRGB(255,255,255)
    fx.colors[1] = CHSV(0, 0, 255);
    fx.precompute(1000);  // updatePositions() is a no-op, so positions above stick;
                          // this only refreshes the cached CartesianCoordinates.

    LedStrip& strip = GEOMETRY.getStrip(0);
    Led led = strip.leds[0];
    led.cartesian = Vec2f(0, 0).toCartesian();

    CRGB result = fx.evaluate(&strip, &led, 0, 1000);
    // Two coincident full-brightness white emitters, additively blended and
    // channel-saturated, should be at (or very near) full white.
    TEST_ASSERT_TRUE(result.r > 250);
    TEST_ASSERT_TRUE(result.g > 250);
    TEST_ASSERT_TRUE(result.b > 250);
}

void test_emitter_field_effect_distant_emitter_contributes_almost_nothing()
{
    NoOpEmitterField fx(1);
    fx.positions[0] = Vec2f((float)GEOMETRY.getScreenRadius() * 20.0f, 0);
    fx.colors[0] = CHSV(0, 255, 255);
    fx.precompute(1000);  // updatePositions() is a no-op, so the position above sticks;
                          // this only refreshes the cached CartesianCoordinates.

    LedStrip& strip = GEOMETRY.getStrip(0);
    Led led = strip.leds[0];
    led.cartesian = Vec2f(0, 0).toCartesian();

    CRGB result = fx.evaluate(&strip, &led, 0, 1000);
    int sum = (int)result.r + result.g + result.b;
    TEST_ASSERT_TRUE(sum < 5);
}

// ---------------------------------------------------------------------------
// BezierPath
// ---------------------------------------------------------------------------

void test_bezier_evaluate_cubic_endpoints_match_control_points()
{
    Vec2f p0(0, 0), p1(1, 5), p2(4, 5), p3(5, 0);
    Vec2f atStart = BezierPath::evaluateCubic(p0, p1, p2, p3, 0.0f);
    Vec2f atEnd = BezierPath::evaluateCubic(p0, p1, p2, p3, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, p0.x, atStart.x);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, p0.y, atStart.y);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, p3.x, atEnd.x);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, p3.y, atEnd.y);
}

void test_bezier_ease_quintic_endpoints_and_monotonic()
{
    TEST_ASSERT_EQUAL_FLOAT(0.0f, BezierPath::easeQuintic(0.0f));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, BezierPath::easeQuintic(1.0f));

    float prev = -1.0f;
    for (int i = 0; i <= 20; i++)
    {
        float t = i / 20.0f;
        float e = BezierPath::easeQuintic(t);
        TEST_ASSERT_TRUE(e >= prev);
        prev = e;
    }
}

void test_bezier_ease_quintic_clamps_outside_unit_range()
{
    TEST_ASSERT_EQUAL_FLOAT(0.0f, BezierPath::easeQuintic(-0.5f));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, BezierPath::easeQuintic(1.5f));
}

void test_bezier_path_tangent_continuous_across_generated_segment()
{
    randomSeed(123);
    BezierPath path;
    Vec2f oldEndTangent = (path.p3 - path.p2).normalized();

    // Force exactly one segment rollover.
    path.step(path.segmentDurationMs + 1);

    Vec2f newStartTangent = (path.p1 - path.p0).normalized();
    // Only meaningful when the old tangent was well-defined (non-degenerate).
    if (oldEndTangent.lengthSquared() > 0.5f)
    {
        TEST_ASSERT_FLOAT_WITHIN(0.01f, oldEndTangent.x, newStartTangent.x);
        TEST_ASSERT_FLOAT_WITHIN(0.01f, oldEndTangent.y, newStartTangent.y);
    }
}

void test_bezier_path_stays_within_screen_bounds()
{
    randomSeed(99);
    float halfW = GEOMETRY.getScreenHalfWidth();
    float halfH = GEOMETRY.getScreenHalfHeight();

    for (int p = 0; p < 5; p++)
    {
        BezierPath path;
        milliseconds_t t = 0;
        for (int i = 0; i < 500; i++)
        {
            path.step(37);
            t += 37;
            Vec2f pos = path.position();
            TEST_ASSERT_TRUE(fabsf(pos.x) <= halfW);
            TEST_ASSERT_TRUE(fabsf(pos.y) <= halfH);
        }
    }
}

void test_bezier_path_large_dt_does_not_get_stuck_past_segment_end()
{
    randomSeed(5);
    BezierPath path;
    path.step(50000);  // spans many segments at once (frame-hitch simulation)
    TEST_ASSERT_TRUE(path.elapsedMs < path.segmentDurationMs);

    Vec2f pos = path.position();
    TEST_ASSERT_FALSE(std::isnan(pos.x));
    TEST_ASSERT_FALSE(std::isnan(pos.y));
    TEST_ASSERT_TRUE(fabsf(pos.x) <= (float)GEOMETRY.getScreenHalfWidth());
    TEST_ASSERT_TRUE(fabsf(pos.y) <= (float)GEOMETRY.getScreenHalfHeight());
}

// Regression test: generateNextSegment() must not reset elapsedMs to 0, or the
// leftover time step()'s while loop just computed gets silently discarded on
// every ordinary segment transition (and a large dt would only ever advance one
// segment instead of catching up through all the ones that elapsed).
void test_bezier_path_step_preserves_leftover_time_across_segment_rollover()
{
    randomSeed(7);
    BezierPath path;
    milliseconds_t duration = path.segmentDurationMs;
    milliseconds_t overshoot = 37;  // well within any SEGMENT_MIN_MS-long next segment

    path.step(duration + overshoot);

    TEST_ASSERT_EQUAL_UINT32(overshoot, path.elapsedMs);
}

int main(int argc, char** argv)
{
    UNITY_BEGIN();

    RUN_TEST(test_vec2f_addition);
    RUN_TEST(test_vec2f_subtraction);
    RUN_TEST(test_vec2f_scalar_multiply_both_orders);
    RUN_TEST(test_vec2f_compound_assignment);
    RUN_TEST(test_vec2f_dot_product);
    RUN_TEST(test_vec2f_length_and_length_squared);
    RUN_TEST(test_vec2f_normalized_unit_vector);
    RUN_TEST(test_vec2f_normalized_zero_vector_is_zero_not_nan);
    RUN_TEST(test_vec2f_cartesian_round_trip);
    RUN_TEST(test_vec2f_to_cartesian_rounds_to_nearest);

    RUN_TEST(test_random_float_stays_within_range);
    RUN_TEST(test_random_float_covers_both_ends_of_range);

    RUN_TEST(test_frame_clock_first_tick_uses_16ms_fallback);
    RUN_TEST(test_frame_clock_reports_real_delta_on_subsequent_ticks);
    RUN_TEST(test_frame_clock_clamps_huge_gap_to_max_dt);

    RUN_TEST(test_emitter_field_effect_blends_colors_additively_at_zero_distance);
    RUN_TEST(test_emitter_field_effect_distant_emitter_contributes_almost_nothing);

    RUN_TEST(test_bezier_evaluate_cubic_endpoints_match_control_points);
    RUN_TEST(test_bezier_ease_quintic_endpoints_and_monotonic);
    RUN_TEST(test_bezier_ease_quintic_clamps_outside_unit_range);
    RUN_TEST(test_bezier_path_tangent_continuous_across_generated_segment);
    RUN_TEST(test_bezier_path_stays_within_screen_bounds);
    RUN_TEST(test_bezier_path_large_dt_does_not_get_stuck_past_segment_end);
    RUN_TEST(test_bezier_path_step_preserves_leftover_time_across_segment_rollover);

    return UNITY_END();
}
