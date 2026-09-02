// Native unit tests for include/ota-eligibility.h - the pure version-code
// arithmetic behind OTA (#63): may this release be flashed, and did the last
// one take?
#include <unity.h>

#include "ota-eligibility.h"

void setUp() {}
void tearDown() {}

// ---- shouldApply() -----------------------------------------------------------

void test_should_apply_allows_a_strictly_newer_build()
{
    // Good weather: the normal case - a newer release is offered.
    TEST_ASSERT_TRUE(
        OtaEligibility::shouldApply(/*latest=*/470, /*running=*/468, /*fsDamaged=*/false));
}

void test_should_apply_refuses_the_same_build_on_a_healthy_device()
{
    // Good weather: a healthy device already on the newest code is not
    // re-flashed - no accidental repair loop.
    TEST_ASSERT_FALSE(OtaEligibility::shouldApply(470, 470, false));
}

void test_should_apply_allows_the_same_build_to_repair_a_damaged_filesystem()
{
    // Bad weather: an interrupted FS write left the device on the right
    // firmware but with no web UI. The equal-version re-flash must be allowed.
    TEST_ASSERT_TRUE(OtaEligibility::shouldApply(468, 468, /*fsDamaged=*/true));
}

void test_should_apply_never_downgrades_even_with_a_damaged_filesystem()
{
    // Bad weather: a damaged filesystem must not become a downgrade vector.
    TEST_ASSERT_FALSE(
        OtaEligibility::shouldApply(/*latest=*/400, /*running=*/468, /*fsDamaged=*/true));
    TEST_ASSERT_FALSE(OtaEligibility::shouldApply(400, 468, false));
}

// ---- didNotTake() ----------------------------------------------------------

void test_did_not_take_flags_a_boot_into_an_older_image_than_recorded()
{
    // Bad weather: we persisted "applied 470" then rebooted into 468.
    TEST_ASSERT_TRUE(OtaEligibility::didNotTake(/*lastApplied=*/470, /*running=*/468));
}

void test_did_not_take_is_quiet_on_a_normal_boot()
{
    // Good weather: recorded code == running code, nothing to report.
    TEST_ASSERT_FALSE(OtaEligibility::didNotTake(470, 470));
}

void test_did_not_take_is_quiet_on_a_never_updated_unit()
{
    // A factory / USB-flashed unit has never persisted an applied code.
    TEST_ASSERT_FALSE(OtaEligibility::didNotTake(0, 468));
}

void test_did_not_take_is_quiet_after_a_successful_newer_update()
{
    // Recorded 470, booted 470+ (a later USB flash) - not a failure.
    TEST_ASSERT_FALSE(OtaEligibility::didNotTake(470, 471));
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_should_apply_allows_a_strictly_newer_build);
    RUN_TEST(test_should_apply_refuses_the_same_build_on_a_healthy_device);
    RUN_TEST(test_should_apply_allows_the_same_build_to_repair_a_damaged_filesystem);
    RUN_TEST(test_should_apply_never_downgrades_even_with_a_damaged_filesystem);
    RUN_TEST(test_did_not_take_flags_a_boot_into_an_older_image_than_recorded);
    RUN_TEST(test_did_not_take_is_quiet_on_a_normal_boot);
    RUN_TEST(test_did_not_take_is_quiet_on_a_never_updated_unit);
    RUN_TEST(test_did_not_take_is_quiet_after_a_successful_newer_update);
    return UNITY_END();
}
