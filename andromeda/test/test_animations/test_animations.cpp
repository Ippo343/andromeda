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

// AbstractBlockingAnimation::GetName()/run() are declared in
// animation-base.h but never defined anywhere in production code - every
// real animation overrides both, so this dead code path never needed a
// definition until a native test needed the base class's vtable to link
// (mirrors test_mission_control.cpp's identical workaround).
const char* AbstractBlockingAnimation::GetName() { return "AbstractBlockingAnimation"; }
void AbstractBlockingAnimation::run() {}

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
        AbstractFrameAnimation* anim = getRandomAnimation();
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
        AbstractFrameAnimation* anim = getRandomAnimation();
        std::string name = anim->GetName();
        delete anim;

        if (i > 0) { TEST_ASSERT_TRUE(name != previous); }
        previous = name;
    }
}

// ---------------------------------------------------------------------------
// Rotation animations (SweepStrips/ClockSweep/RadialSweep/SequentialFadeIn/
// Swipe) - each is now an AbstractFrameAnimation driven by renderFrame(t),
// with no delay()/busy loop of its own, so they're natively testable the
// same way effects are (test_effects.cpp's exerciseEvaluate() pattern).
// ---------------------------------------------------------------------------

// Drives an animation's renderFrame() forward in fixed steps until it
// reports done, asserting that actually happens within maxT - i.e. the
// animation has a real, bounded duration and doesn't spin forever. Returns
// the number of renderFrame() calls made, so callers can also sanity-check
// more than one frame was actually rendered.
static int driveToCompletion(AbstractFrameAnimation& anim, milliseconds_t step = 25,
                             milliseconds_t maxT = 10000)
{
    int frames = 0;
    for (milliseconds_t t = 0; t <= maxT; t += step)
    {
        frames++;
        if (anim.renderFrame(t)) return frames;
    }
    TEST_FAIL_MESSAGE("animation did not report done within maxT");
    return frames;
}

void test_sweep_strips_finishes_within_a_bounded_duration()
{
    SweepStrips anim;
    int frames = driveToCompletion(anim);
    TEST_ASSERT_TRUE(frames > 1);
}

// Regression test: SweepStrips maps segment-elapsed time straight onto LED index.
// SegmentedAnimation never hands a non-final segment's fn the exact segmentT ==
// duration tick (it hands the boundary tick to the *next* segment instead - see
// segmented-animation.h), so without deliberate headroom the sweep always
// undershoots the top of the strip by up to one frame's worth of LEDs - invisible
// on a short strip, but a real dark gap at the tail of a long one (surfaced on the
// 1600-LED grid test rig). Drive at the default (realistic) 25ms step and confirm
// the very last LED of a long single strip does get lit at some point.
void test_sweep_strips_lights_the_last_led_of_a_long_strip()
{
    GEOMETRY.initializeForTest(ModelId::GRID_TEST_DEVICE);
    SweepStrips anim;
    LedStrip& strip = GEOMETRY.getStrip(0);
    size_t lastIdx = strip.num_leds - 1;

    bool sawLit = false;
    for (milliseconds_t t = 0; t <= 10000 && !sawLit; t += 25)
    {
        anim.renderFrame(t);
        if (strip.buffer[lastIdx] != CRGB::Black) sawLit = true;
    }

    TEST_ASSERT_TRUE(sawLit);
}

void test_clock_sweep_finishes_within_a_bounded_duration()
{
    ClockSweep anim;
    int frames = driveToCompletion(anim);
    TEST_ASSERT_TRUE(frames > 1);
}

void test_radial_sweep_finishes_within_a_bounded_duration()
{
    RadialSweep anim;
    int frames = driveToCompletion(anim);
    TEST_ASSERT_TRUE(frames > 1);
}

void test_sequential_fade_in_finishes_within_a_bounded_duration()
{
    GEOMETRY.initializeForTest(
        ModelId::L70_MK1);  // multiple strips, to exercise per-strip segments
    SequentialFadeIn anim;
    int frames = driveToCompletion(anim);
    TEST_ASSERT_TRUE(frames > 1);
}

void test_swipe_finishes_within_a_bounded_duration()
{
    Swipe anim;
    int frames = driveToCompletion(anim);
    TEST_ASSERT_TRUE(frames > 1);
}

// Swipe's trail fade tracks dt between renderFrame() calls internally (see
// accumulateFadeAmount() in animations.cpp), so drive it at a much finer step than the
// default to exercise that dt-tracking path at a very different call cadence and confirm
// it still finishes within a bounded duration.
void test_swipe_finishes_within_a_bounded_duration_at_a_fine_step()
{
    Swipe anim;
    int frames = driveToCompletion(anim, /*step=*/5);
    TEST_ASSERT_TRUE(frames > 1);
}

// Swipe's swept color only lands within a narrow (step-wide) window of v per
// frame, so coarsely-spaced samples (as in the bounded-duration test above)
// can miss every real LED entirely by chance. Sample densely enough across
// the full timeline to actually prove the sweep lights something up, not
// just that it finishes.
void test_swipe_actually_lights_up_a_led_somewhere_during_the_sweep()
{
    Swipe anim;
    LedStrip& strip = GEOMETRY.getStrip(0);

    bool sawLitLed = false;
    for (milliseconds_t t = 0; t <= 1000 && !sawLitLed; t += 1)
    {
        anim.renderFrame(t);
        for (size_t i = 0; i < strip.num_leds; i++)
        {
            if (strip.buffer[i] != CRGB::Black)
            {
                sawLitLed = true;
                break;
            }
        }
    }

    TEST_ASSERT_TRUE(sawLitLed);
}

// A call at t=0 must render the animation's very first frame rather than
// crash or skip straight to a later segment - regression guard for
// SegmentedAnimation's segment-selection math at the very start.
void test_rotation_animations_render_first_frame_without_crashing()
{
    SweepStrips sweepStrips;
    TEST_ASSERT_FALSE(sweepStrips.renderFrame(0));

    ClockSweep clockSweep;
    TEST_ASSERT_FALSE(clockSweep.renderFrame(0));

    RadialSweep radialSweep;
    TEST_ASSERT_FALSE(radialSweep.renderFrame(0));

    SequentialFadeIn sequentialFadeIn;
    TEST_ASSERT_FALSE(sequentialFadeIn.renderFrame(0));

    Swipe swipe;
    TEST_ASSERT_FALSE(swipe.renderFrame(0));
}

// A call far past any animation's real duration must still report done
// (clamped), not silently keep going or crash - regression guard for
// SegmentedAnimation's last-segment clamping.
void test_rotation_animations_report_done_far_past_their_duration()
{
    SweepStrips sweepStrips;
    TEST_ASSERT_TRUE(sweepStrips.renderFrame(60000));

    ClockSweep clockSweep;
    TEST_ASSERT_TRUE(clockSweep.renderFrame(60000));

    RadialSweep radialSweep;
    TEST_ASSERT_TRUE(radialSweep.renderFrame(60000));

    SequentialFadeIn sequentialFadeIn;
    TEST_ASSERT_TRUE(sequentialFadeIn.renderFrame(60000));

    Swipe swipe;
    TEST_ASSERT_TRUE(swipe.renderFrame(60000));
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

    RUN_TEST(test_sweep_strips_finishes_within_a_bounded_duration);
    RUN_TEST(test_sweep_strips_lights_the_last_led_of_a_long_strip);
    RUN_TEST(test_clock_sweep_finishes_within_a_bounded_duration);
    RUN_TEST(test_radial_sweep_finishes_within_a_bounded_duration);
    RUN_TEST(test_sequential_fade_in_finishes_within_a_bounded_duration);
    RUN_TEST(test_swipe_finishes_within_a_bounded_duration);
    RUN_TEST(test_swipe_finishes_within_a_bounded_duration_at_a_fine_step);
    RUN_TEST(test_swipe_actually_lights_up_a_led_somewhere_during_the_sweep);
    RUN_TEST(test_rotation_animations_render_first_frame_without_crashing);
    RUN_TEST(test_rotation_animations_report_done_far_past_their_duration);

    return UNITY_END();
}
