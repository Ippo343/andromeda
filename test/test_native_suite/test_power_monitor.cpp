#include <unity.h>

#include "power-monitor.h"

void test_power_monitor_setUp() {}
void test_power_monitor_tearDown() {}

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

void run_test_power_monitor_tests()
{
    RUN_TEST(test_estimate_current_ma_at_full_brightness);
    RUN_TEST(test_estimate_current_ma_scales_with_applied_brightness);
    RUN_TEST(test_estimate_current_ma_zero_brightness_is_zero);
    RUN_TEST(test_estimate_current_ma_zero_power_is_zero);
    RUN_TEST(test_estimate_current_ma_does_not_overflow_on_a_large_panel);
}
