#include <unity.h>

#include "power-monitor.h"

// Exposes PowerMonitor's private sample buffer to this test file via the
// UNIT_TEST-guarded friend declaration in power-monitor.h, so currentMa()'s
// averaging can be tested deterministically without going through tick()
// (which depends on GEOMETRY/FastLED state).
class PowerMonitorTestAccess
{
   public:
    static void fillUniform(PowerMonitor& pm, uint32_t value)
    {
        for (size_t i = 0; i < 128; i++) pm.samples[i] = value;
    }
    static void setSample(PowerMonitor& pm, size_t i, uint32_t value) { pm.samples[i] = value; }
};

void test_power_monitor_setUp() {}
void test_power_monitor_tearDown()
{
    PowerMonitorTestAccess::fillUniform(PowerMonitor::Instance(), 0);
}

void test_estimate_current_ma_at_full_brightness()
{
    // 5000mW at brightness 255 (unscaled), 5V rail -> 1000mA, no scaling applied.
    TEST_ASSERT_EQUAL_UINT32(1000, estimateCurrentMa(5000, 255));
}

void test_estimate_current_ma_scales_with_applied_brightness()
{
    // brightness 128/255 of full -> ~half the current (integer math, not exactly
    // half since 128/255 != 0.5): 5000mW * 128/255 = 2509mW -> /5V = 501mA.
    TEST_ASSERT_EQUAL_UINT32(501, estimateCurrentMa(5000, 128, 5));
}

void test_estimate_current_ma_zero_brightness_is_zero()
{
    TEST_ASSERT_EQUAL_UINT32(0, estimateCurrentMa(5000, 0));
}

void test_estimate_current_ma_zero_power_is_zero()
{
    TEST_ASSERT_EQUAL_UINT32(0, estimateCurrentMa(0, 255));
}

void test_estimate_current_ma_does_not_overflow_on_a_large_panel()
{
    // A large panel at full white can approach UINT32_MAX in unscaled mW;
    // the uint64_t intermediate must not truncate before the final divide.
    uint32_t large = 4000000000u;
    TEST_ASSERT_EQUAL_UINT32(large / 5, estimateCurrentMa(large, 255));
}

void test_current_ma_averages_uniform_samples()
{
    PowerMonitorTestAccess::fillUniform(PowerMonitor::Instance(), 300);
    TEST_ASSERT_EQUAL_UINT32(300, PowerMonitor::Instance().currentMa());
}

void test_current_ma_averages_varying_samples()
{
    PowerMonitor& pm = PowerMonitor::Instance();
    // Half the samples at 100mA, half at 300mA -> average 200mA.
    for (size_t i = 0; i < 128; i++)
        PowerMonitorTestAccess::setSample(pm, i, (i % 2 == 0) ? 100 : 300);
    TEST_ASSERT_EQUAL_UINT32(200, pm.currentMa());
}

void test_current_ma_is_zero_before_any_tick()
{
    // Mirrors PerformanceMonitor::fps()'s "still-zero slots at boot" trade-off:
    // an unstarted buffer averages to zero, not NaN or garbage.
    TEST_ASSERT_EQUAL_UINT32(0, PowerMonitor::Instance().currentMa());
}

void test_current_ma_does_not_overflow_with_high_readings()
{
    // 128 samples * ~4.2 billion mA each would overflow a uint32_t running sum;
    // the uint64_t total must not truncate before the final divide.
    uint32_t large = 4000000000u;
    PowerMonitorTestAccess::fillUniform(PowerMonitor::Instance(), large);
    TEST_ASSERT_EQUAL_UINT32(large, PowerMonitor::Instance().currentMa());
}

void run_test_power_monitor_tests()
{
    RUN_TEST(test_estimate_current_ma_at_full_brightness);
    RUN_TEST(test_estimate_current_ma_scales_with_applied_brightness);
    RUN_TEST(test_estimate_current_ma_zero_brightness_is_zero);
    RUN_TEST(test_estimate_current_ma_zero_power_is_zero);
    RUN_TEST(test_estimate_current_ma_does_not_overflow_on_a_large_panel);
    RUN_TEST(test_current_ma_averages_uniform_samples);
    RUN_TEST(test_current_ma_averages_varying_samples);
    RUN_TEST(test_current_ma_is_zero_before_any_tick);
    RUN_TEST(test_current_ma_does_not_overflow_with_high_readings);
}
