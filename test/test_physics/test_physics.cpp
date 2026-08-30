#include <unity.h>

#include <algorithm>
#include <cmath>

#include "effects/emitter-field-effect.h"
#include "effects/ring-field-effect.h"
#include "geometry/geometry.h"
#include "physics/bezier-path.h"
#include "physics/box-bounce.h"
#include "physics/frame-clock.h"
#include "physics/nbody-system.h"
#include "physics/physics-random.h"
#include "physics/vec2f.h"
#include "physics/verlet-chain.h"

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

// ---------------------------------------------------------------------------
// VerletChain
// ---------------------------------------------------------------------------

static float rodActualLength(const VerletChain& chain, size_t i)
{
    Vec2f a = i == 0 ? chain.anchor : chain.curr[i - 1];
    return (chain.curr[i] - a).length();
}

static void assertRodLengthsHold(const VerletChain& chain, float tolerance)
{
    for (size_t i = 0; i < chain.curr.size(); i++)
        TEST_ASSERT_FLOAT_WITHIN(chain.rodLength[i] * tolerance, chain.rodLength[i],
                                 rodActualLength(chain, i));
}

static void assertAllFinite(const VerletChain& chain)
{
    for (size_t i = 0; i < chain.curr.size(); i++)
    {
        TEST_ASSERT_FALSE(std::isnan(chain.curr[i].x) || std::isinf(chain.curr[i].x));
        TEST_ASSERT_FALSE(std::isnan(chain.curr[i].y) || std::isinf(chain.curr[i].y));
    }
}

static float chainTotalMechanicalEnergy(const VerletChain& chain, float lastSubstepDtSeconds)
{
    float energy = 0.0f;
    for (size_t i = 0; i < chain.curr.size(); i++)
    {
        float mass = 1.0f / chain.invMass[i];
        Vec2f velocity = (chain.curr[i] - chain.prev[i]) * (1.0f / lastSubstepDtSeconds);
        float ke = 0.5f * mass * velocity.lengthSquared();
        float pe = -mass * VerletChain::GRAVITY_MM_PER_S2 * chain.curr[i].y;
        energy += ke + pe;
    }
    return energy;
}

void test_verlet_chain_preserves_rod_lengths_n2()
{
    randomSeed(11);
    VerletChain chain;
    chain.initRandom(2, Vec2f(0, 0), 50.0f, 100.0f, 1.0f, 3.0f);
    for (int i = 0; i < 10000; i++) chain.step(16);
    assertRodLengthsHold(chain, 0.02f);
    assertAllFinite(chain);
}

void test_verlet_chain_preserves_rod_lengths_n6()
{
    randomSeed(12);
    VerletChain chain;
    chain.initRandom(6, Vec2f(0, 0), 30.0f, 60.0f, 1.0f, 4.0f);
    for (int i = 0; i < 10000; i++) chain.step(16);
    // Looser tolerance than the n=2 case: a fixed Gauss-Seidel iteration count
    // converges less completely on a longer chain, so a bit more per-step slack
    // accumulates over 10,000 steps (160s of simulated time).
    assertRodLengthsHold(chain, 0.05f);
    assertAllFinite(chain);
}

void test_verlet_chain_stable_under_irregular_dt()
{
    randomSeed(13);
    VerletChain chain;
    chain.initRandom(4, Vec2f(0, 0), 40.0f, 80.0f, 1.0f, 3.0f);

    milliseconds_t pattern[] = {16, 200, 5, 1000};
    for (int i = 0; i < 200; i++) chain.step(pattern[i % 4]);

    assertAllFinite(chain);
    assertRodLengthsHold(chain, 0.05f);
}

void test_verlet_chain_bounded_reach_from_anchor()
{
    randomSeed(14);
    VerletChain chain;
    chain.initRandom(5, Vec2f(0, 0), 20.0f, 50.0f, 1.0f, 3.0f);
    float totalRod = 0;
    for (float len : chain.rodLength) totalRod += len;

    for (int i = 0; i < 2000; i++)
    {
        chain.step(16);
        float reach = (chain.curr.back() - chain.anchor).length();
        TEST_ASSERT_TRUE(reach <= totalRod * 1.05f);
    }
}

void test_verlet_chain_energy_stays_bounded_over_long_frictionless_run()
{
    randomSeed(15);
    VerletChain chain;
    chain.initRandom(3, Vec2f(0, 0), 40.0f, 80.0f, 1.0f, 3.0f);

    chain.step(16);  // settle one substep so curr/prev reflect real motion
    float initialEnergy = fabsf(chainTotalMechanicalEnergy(chain, 0.016f));
    float scale = initialEnergy + 1.0f;  // +1 guards against a near-zero initial energy

    for (int i = 0; i < 20000; i++) chain.step(16);

    float laterEnergy = fabsf(chainTotalMechanicalEnergy(chain, 0.016f));
    TEST_ASSERT_TRUE(laterEnergy < scale * 100.0f);
    assertAllFinite(chain);
}

// ---------------------------------------------------------------------------
// NBodySystem
// ---------------------------------------------------------------------------

static bool allFinite(const NBodySystem& sim)
{
    for (size_t i = 0; i < sim.pos.size(); i++)
    {
        if (std::isnan(sim.pos[i].x) || std::isinf(sim.pos[i].x)) return false;
        if (std::isnan(sim.pos[i].y) || std::isinf(sim.pos[i].y)) return false;
        if (std::isnan(sim.vel[i].x) || std::isinf(sim.vel[i].x)) return false;
        if (std::isnan(sim.vel[i].y) || std::isinf(sim.vel[i].y)) return false;
    }
    return true;
}

void test_nbody_zero_net_momentum_after_init()
{
    for (size_t n = 3; n <= 8; n++)
    {
        randomSeed((unsigned long)n * 17 + 3);
        NBodySystem sim;
        sim.initRandom(n, 500.0f, 1.0f, 4.0f, 10.0f, 60.0f);

        Vec2f totalP(0, 0);
        for (size_t i = 0; i < n; i++) totalP += sim.vel[i] * sim.mass[i];

        TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, totalP.x);
        TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, totalP.y);
    }
}

void test_nbody_softening_prevents_blowup_at_near_zero_separation()
{
    NBodySystem sim;
    sim.pos = {Vec2f(0, 0), Vec2f(0.0001f, 0)};
    sim.vel = {Vec2f(0, 0), Vec2f(0, 0)};
    sim.mass = {2.0f, 2.0f};
    sim.boundsRadius = 1000.0f;

    for (int i = 0; i < 50; i++) sim.step(16);
    TEST_ASSERT_TRUE(allFinite(sim));
}

void test_nbody_two_body_orbit_stays_bounded()
{
    NBodySystem sim;
    float m1 = 2.0f, m2 = 3.0f, total = m1 + m2;
    float d0 = 300.0f;

    sim.mass = {m1, m2};
    sim.pos = {Vec2f(-d0 * m2 / total, 0), Vec2f(d0 * m1 / total, 0)};

    float vRel = sqrtf(NBodySystem::G * total / d0);
    sim.vel = {Vec2f(0, -vRel * m2 / total), Vec2f(0, vRel * m1 / total)};
    sim.boundsRadius = 1000.0f;  // comfortably larger than the orbit radius

    float period = 2.0f * PI * sqrtf(d0 * d0 * d0 / (NBodySystem::G * total));
    int steps = (int)(3.0f * period * 1000.0f / 16.0f);  // simulate ~3 orbital periods

    float minSep = 1e9f, maxSep = 0;
    for (int i = 0; i < steps; i++)
    {
        sim.step(16);
        float sep = (sim.pos[1] - sim.pos[0]).length();
        minSep = fminf(minSep, sep);
        maxSep = fmaxf(maxSep, sep);
    }

    TEST_ASSERT_TRUE(allFinite(sim));
    TEST_ASSERT_TRUE(minSep > d0 * 0.3f);
    TEST_ASSERT_TRUE(maxSep < d0 * 3.0f);
}

void test_nbody_reseeds_when_a_body_permanently_escapes()
{
    randomSeed(21);
    NBodySystem sim;
    sim.initRandom(3, 300.0f, 1.0f, 3.0f, 10.0f, 30.0f);

    // Force body 0 far outside the bounds, moving further outward with clearly
    // positive specific energy (unbound).
    sim.pos[0] = Vec2f(sim.boundsRadius * 10.0f, 0);
    sim.vel[0] = Vec2f(5000.0f, 0);

    bool reseeded = sim.step(16);
    TEST_ASSERT_TRUE(reseeded);

    for (size_t i = 0; i < sim.pos.size(); i++)
        TEST_ASSERT_TRUE(sim.pos[i].length() <= sim.boundsRadius * 0.6f);
}

// Regression test: detectEscape()'s center-of-mass must exclude the body under test.
// Including it pulls the COM toward that body, understating its true distance from
// "the rest of the system" and making its potential energy look more bound than it
// really is - masking a genuine escape. Numbers below are chosen so this case only
// registers as an escape once the escaping body is excluded from its own COM.
void test_nbody_detect_escape_excludes_self_from_center_of_mass()
{
    NBodySystem sim;
    sim.initRandom(2, 100.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    sim.mass = {1.0f, 1.0f};
    sim.boundsRadius = 100.0f;
    sim.pos = {Vec2f(300.0f, 0.0f), Vec2f(0.0f, 0.0f)};
    sim.vel = {Vec2f(sqrtf(16000.0f), 0.0f), Vec2f(0.0f, 0.0f)};

    bool reseeded = sim.step(0);
    TEST_ASSERT_TRUE(reseeded);
}

void test_nbody_bound_system_never_reseeds()
{
    randomSeed(22);
    NBodySystem sim;
    // Deliberately slow initial speeds relative to typical orbital/escape velocities
    // at these separations (with this G/mass range), so the system should stay bound
    // (helped further by the centering leash) for the whole run.
    sim.initRandom(3, 500.0f, 1.0f, 3.0f, 5.0f, 15.0f);

    for (int i = 0; i < 2000; i++)
    {
        bool reseeded = sim.step(16);
        TEST_ASSERT_FALSE(reseeded);
    }
    TEST_ASSERT_TRUE(allFinite(sim));
}

// ---------------------------------------------------------------------------
// BoxBounce
// ---------------------------------------------------------------------------

void test_box_bounce_stays_within_bounds_over_long_run()
{
    randomSeed(7);
    BoxBounce box;
    box.initRandom(4, 60.0f, 40.0f, 200.0f, 600.0f);  // fast, so it hits walls often

    for (int frame = 0; frame < 3000; frame++)
    {
        box.step(16);
        for (auto& p : box.pos)
        {
            TEST_ASSERT_TRUE(p.x <= 60.0f + 1e-3f && p.x >= -60.0f - 1e-3f);
            TEST_ASSERT_TRUE(p.y <= 40.0f + 1e-3f && p.y >= -40.0f - 1e-3f);
        }
    }
}

void test_box_bounce_conserves_speed()
{
    randomSeed(8);
    BoxBounce box;
    box.initRandom(3, 50.0f, 50.0f, 300.0f, 300.0f);  // every ball starts at speed 300

    for (int frame = 0; frame < 1000; frame++) box.step(16);

    // Reflections only flip a velocity component's sign, so |v| is invariant.
    for (auto& v : box.vel) TEST_ASSERT_FLOAT_WITHIN(1.0f, 300.0f, v.length());
}

// ---------------------------------------------------------------------------
// RingFieldEffect
// ---------------------------------------------------------------------------

// Minimal subclass: a single channel that just diffuses, so the base class's
// storage / periodic Laplacian / substep / palette-render path is exercised
// without depending on any real effect.
class DiffuseRing : public RingFieldEffect
{
   public:
    DiffuseRing() : RingFieldEffect(1) {}
    const char* GetName() override { return "DiffuseRing"; }

    void seedField(size_t strip) override
    {
        // A single hot cell in the middle of each strip.
        channel(strip, 0)[stripLen(strip) / 2] = 255.0f;
    }
    void stepStrip(size_t strip, float dtSeconds) override
    {
        auto& f = channel(strip, 0);
        std::vector<float> next = f;
        for (size_t i = 0; i < f.size(); i++) next[i] = f[i] + 8.0f * laplacian(f, i) * dtSeconds;
        f = next;
    }
    uint8_t colorIndex(size_t strip, size_t led) const override
    {
        return (uint8_t)constrain(channel(strip, 0)[led], 0.0f, 255.0f);
    }

    // Test-only accessors for the base's protected surface.
    static float lap(const std::vector<float>& f, size_t i) { return laplacian(f, i); }
    float at(size_t strip, size_t led) const { return channel(strip, 0)[led]; }
    uint8_t colorIndexAt(size_t strip, size_t led) const { return colorIndex(strip, led); }
    const CRGBPalette256& paletteRef() const { return palette_; }
};

void test_ring_field_laplacian_is_periodic()
{
    std::vector<float> f = {1.0f, 0.0f, 0.0f, 0.0f};
    // At index 0 the left neighbour must wrap to index 3 (value 0):
    // 0 - 2*1 + 0 = -2.
    TEST_ASSERT_EQUAL_FLOAT(-2.0f, DiffuseRing::lap(f, 0));
    // At index 3 the right neighbour wraps back to index 0 (value 1):
    // 0 - 2*0 + 1 = 1.
    TEST_ASSERT_EQUAL_FLOAT(1.0f, DiffuseRing::lap(f, 3));
}

void test_ring_field_diffuses_and_conserves_total_on_a_ring()
{
    GEOMETRY.initializeForTest(ModelId::L10_MK2);
    DiffuseRing fx;
    size_t n = GEOMETRY.getStrip(0).num_leds;
    size_t mid = n / 2;

    // First frame lazily seeds the field (one hot cell = 255) and takes one step.
    fx.precompute(1000);
    float before = 0;
    for (size_t i = 0; i < n; i++) before += fx.at(0, i);

    for (int frame = 1; frame < 50; frame++) fx.precompute(1000 + frame * 16);

    float after = 0;
    for (size_t i = 0; i < n; i++) after += fx.at(0, i);

    // Pure diffusion on a closed loop conserves the integral of the field...
    TEST_ASSERT_FLOAT_WITHIN(1.0f, before, after);
    // ...and spreads the initial spike to its neighbours.
    TEST_ASSERT_TRUE(fx.at(0, mid) < 255.0f);
    TEST_ASSERT_TRUE(fx.at(0, mid - 1) > 0.0f);
    TEST_ASSERT_TRUE(fx.at(0, mid + 1) > 0.0f);
}

void test_ring_field_evaluate_uses_palette_lookup()
{
    GEOMETRY.initializeForTest(ModelId::L10_MK2);
    DiffuseRing fx;
    fx.precompute(1000);

    LedStrip& strip = GEOMETRY.getStrip(0);
    size_t mid = strip.num_leds / 2;
    CRGB got = fx.evaluate(&strip, &strip.leds[mid], mid, 1000);
    CRGB want = ColorFromPalette(fx.paletteRef(), fx.colorIndexAt(0, mid));
    TEST_ASSERT_TRUE(got == want);
}

void test_ring_field_skips_strips_shorter_than_three_cells()
{
    // Andromeda MK1's centre strip is the real short-strip case: stepping it
    // through the periodic Laplacian would be meaningless, so the base must
    // leave sub-3-LED strips untouched instead of reading out of bounds.
    GEOMETRY.initializeForTest(ModelId::ANDROMEDA_MK1);
    DiffuseRing fx;
    for (int frame = 0; frame < 10; frame++) fx.precompute(1000 + frame * 16);
    // Reaching here without an ASan/UBSan trip is the assertion; also sanity
    // check every strip stayed finite.
    for (size_t s = 0; s < GEOMETRY.getNumStrips(); s++)
        for (size_t i = 0; i < GEOMETRY.getStrip(s).num_leds; i++)
            TEST_ASSERT_TRUE(std::isfinite(fx.at(s, i)));
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

    RUN_TEST(test_verlet_chain_preserves_rod_lengths_n2);
    RUN_TEST(test_verlet_chain_preserves_rod_lengths_n6);
    RUN_TEST(test_verlet_chain_stable_under_irregular_dt);
    RUN_TEST(test_verlet_chain_bounded_reach_from_anchor);
    RUN_TEST(test_verlet_chain_energy_stays_bounded_over_long_frictionless_run);

    RUN_TEST(test_nbody_zero_net_momentum_after_init);
    RUN_TEST(test_nbody_softening_prevents_blowup_at_near_zero_separation);
    RUN_TEST(test_nbody_two_body_orbit_stays_bounded);
    RUN_TEST(test_nbody_reseeds_when_a_body_permanently_escapes);
    RUN_TEST(test_nbody_detect_escape_excludes_self_from_center_of_mass);
    RUN_TEST(test_nbody_bound_system_never_reseeds);

    RUN_TEST(test_box_bounce_stays_within_bounds_over_long_run);
    RUN_TEST(test_box_bounce_conserves_speed);

    RUN_TEST(test_ring_field_laplacian_is_periodic);
    RUN_TEST(test_ring_field_diffuses_and_conserves_total_on_a_ring);
    RUN_TEST(test_ring_field_evaluate_uses_palette_lookup);
    RUN_TEST(test_ring_field_skips_strips_shorter_than_three_cells);

    return UNITY_END();
}
