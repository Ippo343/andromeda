#include <unity.h>

#include <algorithm>
#include <cmath>

#include "effects/emitter-field-effect.h"
#include "geometry/geometry.h"
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

    return UNITY_END();
}
