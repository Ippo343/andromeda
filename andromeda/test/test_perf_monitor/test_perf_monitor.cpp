#include <unity.h>

#include <cmath>

#include "perf-monitor.h"

// Exposes PerformanceMonitor's private sample buffer to this test file via
// the UNIT_TEST-guarded friend declaration in perf-monitor.h, so fps() can
// be tested deterministically instead of depending on real elapsed millis()
// between tick() calls.
class PerformanceMonitorTestAccess
{
   public:
    static void fillUniform(PerformanceMonitor& pm, unsigned short value)
    {
        for (size_t i = 0; i < 128; i++) pm.frameTimes[i] = value;
    }
    static void setSample(PerformanceMonitor& pm, size_t i, unsigned short value)
    {
        pm.frameTimes[i] = value;
    }
    static void setLastFrameTime(PerformanceMonitor& pm, unsigned short v) { pm.lastFrameTime = v; }
    static unsigned short lastFrameTime(PerformanceMonitor& pm) { return pm.lastFrameTime; }
    static size_t frameIndex(PerformanceMonitor& pm) { return pm.frameIndex; }
};

void setUp() { PerformanceMonitor::Instance().stop(); }
void tearDown() {}

void test_fps_is_nan_when_no_samples_recorded()
{
    float fps = PerformanceMonitor::Instance().fps();
    TEST_ASSERT_TRUE(std::isnan(fps));
}

void test_fps_computes_from_uniform_samples()
{
    PerformanceMonitorTestAccess::fillUniform(PerformanceMonitor::Instance(), 20);
    // avgFrameTime = 20ms -> fps = 1000/20 = 50
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, PerformanceMonitor::Instance().fps());
}

void test_fps_computes_from_varying_samples()
{
    PerformanceMonitor& pm = PerformanceMonitor::Instance();
    // Half the samples at 10ms, half at 30ms -> average 20ms -> fps = 50
    for (size_t i = 0; i < 128; i++)
        PerformanceMonitorTestAccess::setSample(pm, i, (i % 2 == 0) ? 10 : 30);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, pm.fps());
}

void test_tick_skips_recording_immediately_after_reset()
{
    PerformanceMonitor& pm = PerformanceMonitor::Instance();
    TEST_ASSERT_EQUAL_UINT16(0, PerformanceMonitorTestAccess::lastFrameTime(pm));

    pm.tick();  // lastFrameTime starts at 0 -> the "if (lastFrameTime != 0)" branch is skipped

    // No sample should have been recorded (frameIndex must not have advanced),
    // but lastFrameTime must now be set for the next tick() to use.
    TEST_ASSERT_EQUAL_INT(0, PerformanceMonitorTestAccess::frameIndex(pm));
}

void test_tick_records_interval_on_subsequent_call()
{
    // tick() genuinely depends on real elapsed millis(): this whole test
    // binary can run in under a millisecond, so two back-to-back tick()
    // calls may both observe millis() == 0 - not a bug, just real timing.
    // Force a nonzero "previous" timestamp via the friend accessor instead
    // of relying on wall-clock time actually advancing between calls.
    PerformanceMonitor& pm = PerformanceMonitor::Instance();
    PerformanceMonitorTestAccess::setLastFrameTime(pm, 1);

    pm.tick();  // lastFrameTime (1) != 0 -> this call must record a sample

    TEST_ASSERT_EQUAL_INT(1, PerformanceMonitorTestAccess::frameIndex(pm));
}

void test_stop_resets_all_state()
{
    PerformanceMonitor& pm = PerformanceMonitor::Instance();
    PerformanceMonitorTestAccess::fillUniform(pm, 15);
    pm.tick();
    TEST_ASSERT_FALSE(std::isnan(pm.fps()));

    pm.stop();

    TEST_ASSERT_TRUE(std::isnan(pm.fps()));
    TEST_ASSERT_EQUAL_INT(0, PerformanceMonitorTestAccess::frameIndex(pm));
    TEST_ASSERT_EQUAL_UINT16(0, PerformanceMonitorTestAccess::lastFrameTime(pm));
}

int main(int argc, char** argv)
{
    UNITY_BEGIN();

    RUN_TEST(test_fps_is_nan_when_no_samples_recorded);
    RUN_TEST(test_fps_computes_from_uniform_samples);
    RUN_TEST(test_fps_computes_from_varying_samples);
    RUN_TEST(test_tick_skips_recording_immediately_after_reset);
    RUN_TEST(test_tick_records_interval_on_subsequent_call);
    RUN_TEST(test_stop_resets_all_state);

    return UNITY_END();
}
