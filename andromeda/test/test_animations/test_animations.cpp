#include <unity.h>

#include <set>
#include <string>

// animations.cpp declares most of its animation classes locally (not in a
// header), so we #include the .cpp directly to reach them for white-box
// testing of getRandomAnimation() - mirrors test_effects.cpp's approach for
// getRandomEffect(). animations.cpp is deliberately excluded from
// platformio.ini's native build_src_filter since its run() methods drive
// real delay()/millis() loops (out of scope for native tests); this test
// only exercises getRandomAnimation()'s selection logic, never run().
#include "../../src/animations.cpp"

// AbstractAnimation::GetName()/run() are declared in animation-base.h but
// never defined anywhere in production code - every real animation
// overrides both, so this dead code path never needed a definition until a
// native test needed the base class's vtable to link (mirrors
// test_mission_control.cpp's identical workaround).
const char* AbstractAnimation::GetName() { return "AbstractAnimation"; }
void AbstractAnimation::run() {}

void setUp() { GEOMETRY.initializeForTest(ModelId::SINGLE_STRIP_TEST_DEVICE); }
void tearDown() {}

// ---------------------------------------------------------------------------
// computeSweepRamp() - BaseSweep::colorSweep's per-LED boundary math,
// extracted to animation-utils.h. Covers all four boundary-handling
// branches (angular wraparound at the leading edge, angular wraparound at a
// negative tail, and their non-wraparound radial equivalents) plus the
// normal in-range case.
// ---------------------------------------------------------------------------

void test_sweep_ramp_normal_case_no_boundary_issues()
{
    // Radial (maxCoord != FULL_CIRCLE), coordLead/coordTail both in range.
    SweepRampResult inside = computeSweepRamp(500, 300, 400, 1000, 200);
    TEST_ASSERT_TRUE(inside.inRange);
    TEST_ASSERT_EQUAL_INT(100, inside.rampDistance);

    SweepRampResult belowTail = computeSweepRamp(500, 300, 200, 1000, 200);
    TEST_ASSERT_FALSE(belowTail.inRange);

    SweepRampResult aboveLead = computeSweepRamp(500, 300, 600, 1000, 200);
    TEST_ASSERT_FALSE(aboveLead.inRange);
}

void test_sweep_ramp_radial_overflow_at_leading_edge_has_no_wraparound()
{
    // coordLead > maxCoord, radial: in-range means coord >= coordTail (no wrap).
    SweepRampResult inside = computeSweepRamp(1100, 900, 950, 1000, 200);
    TEST_ASSERT_TRUE(inside.inRange);
    TEST_ASSERT_EQUAL_INT(150, inside.rampDistance);

    SweepRampResult outside = computeSweepRamp(1100, 900, 850, 1000, 200);
    TEST_ASSERT_FALSE(outside.inRange);
}

void test_sweep_ramp_radial_negative_tail_has_no_wraparound()
{
    // coordTail < 0, radial: in-range means coord <= coordLead (no wrap).
    SweepRampResult inside = computeSweepRamp(100, -100, 50, 1000, 200);
    TEST_ASSERT_TRUE(inside.inRange);
    TEST_ASSERT_EQUAL_INT(50, inside.rampDistance);

    SweepRampResult outside = computeSweepRamp(100, -100, 150, 1000, 200);
    TEST_ASSERT_FALSE(outside.inRange);
}

void test_sweep_ramp_angular_overflow_at_leading_edge_wraps()
{
    // maxCoord == FULL_CIRCLE, coordLead > maxCoord: wraps around to the
    // start of the circle instead of just extending past maxCoord.
    SweepRampResult unwrapped = computeSweepRamp(36500, 35500, 35800, FULL_CIRCLE, 1000);
    TEST_ASSERT_TRUE(unwrapped.inRange);
    TEST_ASSERT_EQUAL_INT(700, unwrapped.rampDistance);

    SweepRampResult wrapped = computeSweepRamp(36500, 35500, 200, FULL_CIRCLE, 1000);
    TEST_ASSERT_TRUE(wrapped.inRange);
    TEST_ASSERT_EQUAL_INT(300, wrapped.rampDistance);

    SweepRampResult outside = computeSweepRamp(36500, 35500, 1000, FULL_CIRCLE, 1000);
    TEST_ASSERT_FALSE(outside.inRange);
}

void test_sweep_ramp_angular_negative_tail_wraps()
{
    // maxCoord == FULL_CIRCLE, coordTail < 0: wraps to the end of the circle.
    SweepRampResult wrapped = computeSweepRamp(500, -500, 35800, FULL_CIRCLE, 1000);
    TEST_ASSERT_TRUE(wrapped.inRange);
    TEST_ASSERT_EQUAL_INT(700, wrapped.rampDistance);

    SweepRampResult unwrapped = computeSweepRamp(500, -500, 200, FULL_CIRCLE, 1000);
    TEST_ASSERT_TRUE(unwrapped.inRange);
    TEST_ASSERT_EQUAL_INT(300, unwrapped.rampDistance);

    SweepRampResult outside = computeSweepRamp(500, -500, 1000, FULL_CIRCLE, 1000);
    TEST_ASSERT_FALSE(outside.inRange);
}

// ---------------------------------------------------------------------------
// getRandomAnimation() factory
// ---------------------------------------------------------------------------

void test_get_random_animation_produces_valid_animations()
{
    std::set<std::string> namesSeen;
    for (int i = 0; i < 300; i++)
    {
        AbstractAnimation* anim = getRandomAnimation();
        TEST_ASSERT_NOT_NULL(anim);
        TEST_ASSERT_NOT_NULL(anim->GetName());
        namesSeen.insert(anim->GetName());
        delete anim;
    }
    // 5 possible animations; 300 draws (never repeating consecutively) should
    // realistically hit all of them.
    TEST_ASSERT_TRUE(namesSeen.size() >= 4);
}

void test_get_random_animation_never_repeats_consecutively()
{
    std::string previous;
    for (int i = 0; i < 100; i++)
    {
        AbstractAnimation* anim = getRandomAnimation();
        std::string name = anim->GetName();
        delete anim;

        if (i > 0) { TEST_ASSERT_TRUE(name != previous); }
        previous = name;
    }
}

int main(int argc, char** argv)
{
    UNITY_BEGIN();

    RUN_TEST(test_sweep_ramp_normal_case_no_boundary_issues);
    RUN_TEST(test_sweep_ramp_radial_overflow_at_leading_edge_has_no_wraparound);
    RUN_TEST(test_sweep_ramp_radial_negative_tail_has_no_wraparound);
    RUN_TEST(test_sweep_ramp_angular_overflow_at_leading_edge_wraps);
    RUN_TEST(test_sweep_ramp_angular_negative_tail_wraps);

    RUN_TEST(test_get_random_animation_produces_valid_animations);
    RUN_TEST(test_get_random_animation_never_repeats_consecutively);

    return UNITY_END();
}
