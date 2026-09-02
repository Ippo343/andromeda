#include <unity.h>

#include <set>
#include <vector>

#include "energy-param.h"
#include "utils.h"

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// cmap
// ---------------------------------------------------------------------------

void test_cmap_in_range() { TEST_ASSERT_EQUAL_INT(50, cmap(5, 0, 10, 0, 100)); }

void test_cmap_clamped_low()
{
    // x below in_low maps below out_low and must be clamped up to out_low
    TEST_ASSERT_EQUAL_INT(0, cmap(-5, 0, 10, 0, 100));
}

void test_cmap_clamped_high()
{
    // x above in_high maps above out_high and must be clamped down to out_high
    TEST_ASSERT_EQUAL_INT(100, cmap(15, 0, 10, 0, 100));
}

void test_cmap_negative_output_range() { TEST_ASSERT_EQUAL_INT(-50, cmap(5, 0, 10, -100, 0)); }

void test_cmap_descending_output_range_in_range()
{
    // out_low > out_high (e.g. IndividualStripDrift's inverted energy->duration ranges) -
    // previously constrain(mapped, out_low, out_high) with out_low > out_high always
    // returned out_high regardless of x, collapsing the whole response to a constant.
    TEST_ASSERT_EQUAL_INT(75, cmap(5, 0, 10, 100, 50));
}

void test_cmap_descending_output_range_clamped_at_each_end()
{
    TEST_ASSERT_EQUAL_INT(100, cmap(-5, 0, 10, 100, 50));
    TEST_ASSERT_EQUAL_INT(50, cmap(15, 0, 10, 100, 50));
}

// ---------------------------------------------------------------------------
// Q_rsqrt (fast inverse square root - approximate)
// ---------------------------------------------------------------------------

void test_q_rsqrt_known_values()
{
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, Q_rsqrt(1.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, Q_rsqrt(4.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.25f, Q_rsqrt(16.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.1f, Q_rsqrt(100.0f));
}

// ---------------------------------------------------------------------------
// scaledCubicWave8
// ---------------------------------------------------------------------------

void test_scaled_cubic_wave8_stays_within_bounds()
{
    for (milliseconds_t t = 0; t < 2000; t += 37)
    {
        long v = scaledCubicWave8(t, 1000, -50, 50);
        TEST_ASSERT_TRUE(v >= -50 && v <= 50);
    }
}

void test_scaled_cubic_wave8_period_wraparound_matches()
{
    // t and t+period should land on the same phase within the wave
    long a = scaledCubicWave8(100, 1000, 0, 255);
    long b = scaledCubicWave8(1100, 1000, 0, 255);
    TEST_ASSERT_EQUAL_INT(a, b);
}

void test_scaled_cubic_wave8_zero_min_max()
{
    // minV == maxV should collapse output to that single value
    for (milliseconds_t t = 0; t < 1000; t += 111)
    {
        TEST_ASSERT_EQUAL_INT(7, scaledCubicWave8(t, 500, 7, 7));
    }
}

// ---------------------------------------------------------------------------
// slowSin
// ---------------------------------------------------------------------------

void test_slow_sin_stays_within_bounds()
{
    for (unsigned long ms = 0; ms < 5000; ms += 91)
    {
        float v = slowSin(ms, 30.0f, 10, 200);
        TEST_ASSERT_TRUE(v >= 10.0f && v <= 200.0f);
    }
}

void test_slow_sin_at_zero_is_midpoint()
{
    // sin(0) = 0 -> normalized to 0.5 -> midpoint of [minVal, maxVal]
    float v = slowSin(0, 60.0f, 0, 100);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 50.0f, v);
}

// ---------------------------------------------------------------------------
// shuffle (Fisher-Yates)
// ---------------------------------------------------------------------------

void test_shuffle_is_a_permutation()
{
    randomSeed(12345);
    int arr[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    shuffle(arr, 8);

    std::set<int> seen(arr, arr + 8);
    TEST_ASSERT_EQUAL_INT(8, static_cast<int>(seen.size()));
    for (int i = 0; i < 8; i++) TEST_ASSERT_TRUE(seen.count(i) == 1);
}

void test_shuffle_size_one_is_noop()
{
    int arr[1] = {42};
    shuffle(arr, 1);
    TEST_ASSERT_EQUAL_INT(42, arr[0]);
}

// ---------------------------------------------------------------------------
// py_get (python-style negative indexing)
// ---------------------------------------------------------------------------

void test_py_get_positive_index()
{
    std::vector<int> v = {10, 20, 30};
    TEST_ASSERT_EQUAL_INT(20, py_get(v, 1));
}

void test_py_get_negative_index()
{
    std::vector<int> v = {10, 20, 30};
    TEST_ASSERT_EQUAL_INT(30, py_get(v, -1));
    TEST_ASSERT_EQUAL_INT(10, py_get(v, -3));
}

void test_py_get_wraparound_index()
{
    std::vector<int> v = {10, 20, 30};
    // index == size wraps to 0
    TEST_ASSERT_EQUAL_INT(10, py_get(v, 3));
    // index further beyond size wraps too
    TEST_ASSERT_EQUAL_INT(20, py_get(v, 4));
}

// ---------------------------------------------------------------------------
// RandBool / RandSign
// ---------------------------------------------------------------------------

void test_rand_bool_values_are_0_or_1()
{
    randomSeed(1);
    for (int i = 0; i < 100; i++)
    {
        RandBool b;
        TEST_ASSERT_TRUE(static_cast<bool>(b) == true || static_cast<bool>(b) == false);
    }
}

void test_rand_sign_never_zero()
{
    randomSeed(2);
    for (int i = 0; i < 200; i++)
    {
        RandSign s;
        char v = static_cast<char>(s);
        TEST_ASSERT_TRUE(v == -1 || v == 1);
    }
}

void test_rand_sign_randomize_never_zero()
{
    randomSeed(3);
    RandSign s;
    for (int i = 0; i < 200; i++)
    {
        s.randomize();
        char v = static_cast<char>(s);
        TEST_ASSERT_TRUE(v == -1 || v == 1);
    }
}

// ---------------------------------------------------------------------------
// seedRNGs
// ---------------------------------------------------------------------------

void test_seed_rngs_runs_and_random_still_works()
{
    seedRNGs();
    // Sanity: normal random() calls still behave within bounds afterward -
    // seedRNGs() itself has no return value/observable state to assert on
    // beyond "doesn't crash and doesn't break the RNGs it reseeds".
    for (int i = 0; i < 20; i++)
    {
        long v = random(0, 10);
        TEST_ASSERT_TRUE(v >= 0 && v < 10);
    }
}

// ---------------------------------------------------------------------------
// ticksFromMs
// ---------------------------------------------------------------------------

void test_ticks_from_ms_matches_pdms_to_ticks_for_delays_in_use()
{
    // Good weather: for every delay the firmware actually passes today, the
    // 64-bit helper must produce the same tick count the 1 kHz FreeRTOS tick
    // would - a tick is a millisecond, so it is an identity.
    TEST_ASSERT_EQUAL_UINT32(0u, ticksFromMs(0));
    TEST_ASSERT_EQUAL_UINT32(1u, ticksFromMs(1));
    TEST_ASSERT_EQUAL_UINT32(10000u, ticksFromMs(10000));  // OtaAutoCheck settle delay
    TEST_ASSERT_EQUAL_UINT32(60000u, ticksFromMs(60000));  // AP-rejoin retry interval
}

void test_ticks_from_ms_does_not_overflow_past_the_32bit_intermediate()
{
    // Bad weather: pdMS_TO_TICKS(x) is (x * configTICK_RATE_HZ) / 1000 in
    // 32-bit TickType_t, so the *intermediate* product wraps once x*1000
    // exceeds 2^32 - even though the final tick count fits in 32 bits fine.
    // 4294968 is the first millisecond value whose *1000 crosses 2^32
    // (4294968000 > 4294967296); the buggy macro returns 704 for it.
    TEST_ASSERT_EQUAL_UINT32(4294968u, ticksFromMs(4294968ull));

    // The real regression: the OTA auto-check's 24-hour re-check delay. The
    // macro turns this into ~500654 ticks (~8m21s), hammering GitHub ~172x a
    // day instead of once.
    TEST_ASSERT_EQUAL_UINT32(86400000u, ticksFromMs(24ull * 60 * 60 * 1000));
}

// ---------------------------------------------------------------------------
// rateToThreshold
// ---------------------------------------------------------------------------

void test_rate_to_threshold_zero_dt_is_zero()
{
    TEST_ASSERT_EQUAL_UINT32(0, rateToThreshold(10.0f, 0, 100000));
}

void test_rate_to_threshold_scales_linearly_with_dt()
{
    uint32_t t1 = rateToThreshold(10.0f, 10, 100000);
    uint32_t t2 = rateToThreshold(10.0f, 20, 100000);
    TEST_ASSERT_EQUAL_UINT32(t1 * 2, t2);
}

void test_rate_to_threshold_scales_linearly_with_rate()
{
    uint32_t t1 = rateToThreshold(5.0f, 10, 100000);
    uint32_t t2 = rateToThreshold(10.0f, 10, 100000);
    TEST_ASSERT_EQUAL_UINT32(t1 * 2, t2);
}

void test_rate_to_threshold_clamps_to_roll_limit()
{
    // A huge dt (e.g. a stalled frame) must not overflow past rollLimit
    TEST_ASSERT_EQUAL_UINT32(100000, rateToThreshold(10.0f, 1000000, 100000));
}

void test_roll_event_zero_rate_never_fires()
{
    for (int i = 0; i < 200; i++) TEST_ASSERT_FALSE(rollEvent(0.0f, 16));
}

void test_roll_event_saturated_rate_always_fires()
{
    // rate*dt/1000 >> 1 saturates the underlying threshold to the roll limit, so
    // every roll must succeed regardless of the RNG draw.
    for (int i = 0; i < 200; i++) TEST_ASSERT_TRUE(rollEvent(1000.0f, 1000));
}

// ---------------------------------------------------------------------------
// accumulateFadeAmount
// ---------------------------------------------------------------------------

void test_accumulate_fade_amount_zero_dt_is_zero()
{
    float debt = 0;
    TEST_ASSERT_EQUAL_UINT8(0, accumulateFadeAmount(debt, 1.0f, 0));
}

void test_accumulate_fade_amount_carries_sub_quantum_loss_over_time()
{
    // A per-call loss far below 1/255 would round to 0 forever if not accumulated.
    // Drive ~1 simulated second of 1ms frames and confirm the fade amount eventually
    // registers, proving the debt carries across calls instead of being discarded.
    float debt = 0;
    bool sawNonzeroFade = false;
    for (int i = 0; i < 1000; i++)
    {
        if (accumulateFadeAmount(debt, 1.0f, 1) > 0)
        {
            sawNonzeroFade = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(sawNonzeroFade);
}

void test_accumulate_fade_amount_matches_single_frame_when_dt_is_large_enough()
{
    // A single call whose loss is well above the 1/255 quantum should register
    // immediately without needing to accumulate across calls.
    float debt = 0;
    uint8_t fadeAmount = accumulateFadeAmount(debt, 1.0f, 1000);  // loss = 1.0 -> full fade
    TEST_ASSERT_EQUAL_UINT8(255, fadeAmount);
}

// ---------------------------------------------------------------------------
// Energy / EnergyParam
// ---------------------------------------------------------------------------

void test_energy_get_set_round_trips()
{
    Energy::set(42);
    TEST_ASSERT_EQUAL_UINT8(42, Energy::get());
}

void test_energy_param_maps_energy_range_to_its_own_range()
{
    EnergyParam<int, 10, 20> p;

    Energy::set(0);
    TEST_ASSERT_EQUAL_INT(10, (int)p);

    Energy::set(255);
    TEST_ASSERT_EQUAL_INT(20, (int)p);
}

int main(int argc, char** argv)
{
    UNITY_BEGIN();

    RUN_TEST(test_cmap_in_range);
    RUN_TEST(test_cmap_clamped_low);
    RUN_TEST(test_cmap_clamped_high);
    RUN_TEST(test_cmap_negative_output_range);
    RUN_TEST(test_cmap_descending_output_range_in_range);
    RUN_TEST(test_cmap_descending_output_range_clamped_at_each_end);

    RUN_TEST(test_q_rsqrt_known_values);

    RUN_TEST(test_scaled_cubic_wave8_stays_within_bounds);
    RUN_TEST(test_scaled_cubic_wave8_period_wraparound_matches);
    RUN_TEST(test_scaled_cubic_wave8_zero_min_max);

    RUN_TEST(test_slow_sin_stays_within_bounds);
    RUN_TEST(test_slow_sin_at_zero_is_midpoint);

    RUN_TEST(test_shuffle_is_a_permutation);
    RUN_TEST(test_shuffle_size_one_is_noop);

    RUN_TEST(test_py_get_positive_index);
    RUN_TEST(test_py_get_negative_index);
    RUN_TEST(test_py_get_wraparound_index);

    RUN_TEST(test_rand_bool_values_are_0_or_1);
    RUN_TEST(test_rand_sign_never_zero);
    RUN_TEST(test_rand_sign_randomize_never_zero);

    RUN_TEST(test_seed_rngs_runs_and_random_still_works);

    RUN_TEST(test_ticks_from_ms_matches_pdms_to_ticks_for_delays_in_use);
    RUN_TEST(test_ticks_from_ms_does_not_overflow_past_the_32bit_intermediate);

    RUN_TEST(test_rate_to_threshold_zero_dt_is_zero);
    RUN_TEST(test_rate_to_threshold_scales_linearly_with_dt);
    RUN_TEST(test_rate_to_threshold_scales_linearly_with_rate);
    RUN_TEST(test_rate_to_threshold_clamps_to_roll_limit);

    RUN_TEST(test_roll_event_zero_rate_never_fires);
    RUN_TEST(test_roll_event_saturated_rate_always_fires);

    RUN_TEST(test_accumulate_fade_amount_zero_dt_is_zero);
    RUN_TEST(test_accumulate_fade_amount_carries_sub_quantum_loss_over_time);
    RUN_TEST(test_accumulate_fade_amount_matches_single_frame_when_dt_is_large_enough);

    RUN_TEST(test_energy_get_set_round_trips);
    RUN_TEST(test_energy_param_maps_energy_range_to_its_own_range);

    return UNITY_END();
}
