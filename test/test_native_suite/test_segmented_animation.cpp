#include <unity.h>

#include <vector>

#include "segmented-animation.h"

void test_segmented_animation_setUp() {}
void test_segmented_animation_tearDown() {}

// Exposes SegmentedAnimation's protected addSegment() to this test file, and
// records every (segmentIndex, segmentT) call so tests can assert on exactly
// which segment ran and with what local time - without depending on any
// particular animation's rendering side effects.
class RecordingAnimation : public SegmentedAnimation
{
   public:
    const char* GetName() override { return "RecordingAnimation"; }

    struct Call
    {
        int segmentIndex;
        milliseconds_t segmentT;
    };
    std::vector<Call> calls;

    void addTrackedSegment(milliseconds_t duration)
    {
        int index = nextIndex++;
        addSegment(duration,
                   [this, index](milliseconds_t segmentT) { calls.push_back({index, segmentT}); });
    }

   private:
    int nextIndex = 0;
};

// ---------------------------------------------------------------------------
// No segments
// ---------------------------------------------------------------------------

void test_no_segments_is_immediately_done()
{
    RecordingAnimation anim;
    TEST_ASSERT_TRUE(anim.renderFrame(0));
    TEST_ASSERT_TRUE(anim.renderFrame(1000));
    TEST_ASSERT_EQUAL_INT(0, anim.calls.size());
}

// ---------------------------------------------------------------------------
// Single segment
// ---------------------------------------------------------------------------

void test_single_segment_runs_until_its_duration_then_reports_done()
{
    RecordingAnimation anim;
    anim.addTrackedSegment(100);

    TEST_ASSERT_FALSE(anim.renderFrame(0));
    TEST_ASSERT_FALSE(anim.renderFrame(50));
    TEST_ASSERT_TRUE(anim.renderFrame(100));

    TEST_ASSERT_EQUAL_INT(3, anim.calls.size());
    TEST_ASSERT_EQUAL_INT(0, anim.calls[0].segmentIndex);
    TEST_ASSERT_EQUAL_UINT32(0, anim.calls[0].segmentT);
    TEST_ASSERT_EQUAL_UINT32(50, anim.calls[1].segmentT);
    TEST_ASSERT_EQUAL_UINT32(100, anim.calls[2].segmentT);
}

void test_single_segment_past_its_duration_clamps_segment_t_and_stays_done()
{
    RecordingAnimation anim;
    anim.addTrackedSegment(100);

    TEST_ASSERT_TRUE(anim.renderFrame(500));

    TEST_ASSERT_EQUAL_INT(1, anim.calls.size());
    // segmentT must clamp to the segment's own duration, never run past it,
    // even though localT is way beyond the animation's total duration.
    TEST_ASSERT_EQUAL_UINT32(100, anim.calls[0].segmentT);
}

// ---------------------------------------------------------------------------
// Multiple segments - the "which phase am I in" bookkeeping
// ---------------------------------------------------------------------------

void test_multiple_segments_hand_off_at_the_exact_boundary()
{
    RecordingAnimation anim;
    anim.addTrackedSegment(100);  // segment 0: [0, 100)
    anim.addTrackedSegment(50);   // segment 1: [100, 150]

    anim.renderFrame(0);
    anim.renderFrame(99);
    // Exactly at the boundary: ownership hands off to segment 1 at its own
    // local t=0, not segment 0's t=100 - avoids double-counting the boundary
    // frame across two segments.
    anim.renderFrame(100);
    anim.renderFrame(125);
    bool done = anim.renderFrame(150);

    TEST_ASSERT_TRUE(done);
    TEST_ASSERT_EQUAL_INT(5, anim.calls.size());

    TEST_ASSERT_EQUAL_INT(0, anim.calls[0].segmentIndex);
    TEST_ASSERT_EQUAL_UINT32(0, anim.calls[0].segmentT);
    TEST_ASSERT_EQUAL_INT(0, anim.calls[1].segmentIndex);
    TEST_ASSERT_EQUAL_UINT32(99, anim.calls[1].segmentT);

    TEST_ASSERT_EQUAL_INT(1, anim.calls[2].segmentIndex);
    TEST_ASSERT_EQUAL_UINT32(0, anim.calls[2].segmentT);
    TEST_ASSERT_EQUAL_INT(1, anim.calls[3].segmentIndex);
    TEST_ASSERT_EQUAL_UINT32(25, anim.calls[3].segmentT);
    TEST_ASSERT_EQUAL_INT(1, anim.calls[4].segmentIndex);
    TEST_ASSERT_EQUAL_UINT32(50, anim.calls[4].segmentT);
}

void test_only_finished_after_the_last_segment_ends()
{
    RecordingAnimation anim;
    anim.addTrackedSegment(100);
    anim.addTrackedSegment(50);

    // Reaching the end of segment 0 must NOT report done - segment 1 still
    // has to run.
    TEST_ASSERT_FALSE(anim.renderFrame(100));
    TEST_ASSERT_FALSE(anim.renderFrame(149));
    TEST_ASSERT_TRUE(anim.renderFrame(150));
}

void test_three_segments_of_uneven_duration_route_correctly()
{
    RecordingAnimation anim;
    anim.addTrackedSegment(10);   // [0, 10)
    anim.addTrackedSegment(200);  // [10, 210)
    anim.addTrackedSegment(5);    // [210, 215]

    anim.renderFrame(5);
    anim.renderFrame(150);
    bool done = anim.renderFrame(213);

    TEST_ASSERT_FALSE(done);
    TEST_ASSERT_EQUAL_INT(3, anim.calls.size());
    TEST_ASSERT_EQUAL_INT(0, anim.calls[0].segmentIndex);
    TEST_ASSERT_EQUAL_INT(1, anim.calls[1].segmentIndex);
    TEST_ASSERT_EQUAL_UINT32(140, anim.calls[1].segmentT);
    TEST_ASSERT_EQUAL_INT(2, anim.calls[2].segmentIndex);
    TEST_ASSERT_EQUAL_UINT32(3, anim.calls[2].segmentT);

    TEST_ASSERT_TRUE(anim.renderFrame(215));
}

void run_test_segmented_animation_tests()
{
    RUN_TEST(test_no_segments_is_immediately_done);

    RUN_TEST(test_single_segment_runs_until_its_duration_then_reports_done);
    RUN_TEST(test_single_segment_past_its_duration_clamps_segment_t_and_stays_done);

    RUN_TEST(test_multiple_segments_hand_off_at_the_exact_boundary);
    RUN_TEST(test_only_finished_after_the_last_segment_ends);
    RUN_TEST(test_three_segments_of_uneven_duration_route_correctly);
}
