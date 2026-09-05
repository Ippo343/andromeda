#include <unity.h>

#include "mdns-hosts.h"

using MdnsHosts::buildHostList;
using MdnsHosts::MAX_HOSTS;
using MdnsHosts::resolveFallback;

void test_mdns_hosts_setUp() {}
void test_mdns_hosts_tearDown() {}

void test_distinct_pair_winners_are_all_listed_in_order()
{
    const char* out[MAX_HOSTS];
    size_t n = buildHostList("kitchen", "l70-a1b2", "andromeda", out);
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL_STRING("kitchen", out[0]);
    TEST_ASSERT_EQUAL_STRING("l70-a1b2", out[1]);
    TEST_ASSERT_EQUAL_STRING("andromeda", out[2]);
}

void test_unrenamed_device_collapses_device_and_model_winners()
{
    // A never-renamed device: the device-pair winner is the same "<model>-<uid>" label as the
    // model-pair winner (both derive from DeviceIdentity::getDefaultName()).
    const char* out[MAX_HOSTS];
    size_t n = buildHostList("l70-a1b2", "l70-a1b2", "andromeda", out);
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_EQUAL_STRING("l70-a1b2", out[0]);
    TEST_ASSERT_EQUAL_STRING("andromeda", out[1]);
}

void test_andromeda_mk0_collapses_model_and_andromeda_winners()
{
    // ANDROMEDA_MK0 (and the unresolvable-model fallback) both derive "andromeda[-<uid>]" for
    // both the model-pair and andromeda-pair winners.
    const char* out[MAX_HOSTS];
    size_t n = buildHostList("kitchen", "andromeda-a1b2", "andromeda-a1b2", out);
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_EQUAL_STRING("kitchen", out[0]);
    TEST_ASSERT_EQUAL_STRING("andromeda-a1b2", out[1]);
}

void test_all_three_winners_identical_collapses_to_one()
{
    const char* out[MAX_HOSTS];
    size_t n = buildHostList("andromeda-a1b2", "andromeda-a1b2", "andromeda-a1b2", out);
    TEST_ASSERT_EQUAL(1, n);
    TEST_ASSERT_EQUAL_STRING("andromeda-a1b2", out[0]);
}

void test_empty_winner_is_skipped_not_listed_as_blank()
{
    const char* out[MAX_HOSTS];
    size_t n = buildHostList("kitchen", "", "andromeda-a1b2", out);
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_EQUAL_STRING("kitchen", out[0]);
    TEST_ASSERT_EQUAL_STRING("andromeda-a1b2", out[1]);
}

void test_null_winner_is_skipped()
{
    const char* out[MAX_HOSTS];
    size_t n = buildHostList("kitchen", nullptr, "andromeda-a1b2", out);
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_EQUAL_STRING("kitchen", out[0]);
    TEST_ASSERT_EQUAL_STRING("andromeda-a1b2", out[1]);
}

void test_resolve_fallback_picks_primary_when_free()
{
    TEST_ASSERT_EQUAL_STRING("kitchen", resolveFallback(false, "kitchen", "kitchen-a1b2"));
}

void test_resolve_fallback_picks_fallback_when_taken()
{
    TEST_ASSERT_EQUAL_STRING("kitchen-a1b2", resolveFallback(true, "kitchen", "kitchen-a1b2"));
}

void run_test_mdns_hosts_tests()
{
    RUN_TEST(test_distinct_pair_winners_are_all_listed_in_order);
    RUN_TEST(test_unrenamed_device_collapses_device_and_model_winners);
    RUN_TEST(test_andromeda_mk0_collapses_model_and_andromeda_winners);
    RUN_TEST(test_all_three_winners_identical_collapses_to_one);
    RUN_TEST(test_empty_winner_is_skipped_not_listed_as_blank);
    RUN_TEST(test_null_winner_is_skipped);
    RUN_TEST(test_resolve_fallback_picks_primary_when_free);
    RUN_TEST(test_resolve_fallback_picks_fallback_when_taken);
}
