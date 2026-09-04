#include <unity.h>

#include "mdns-hosts.h"

using MdnsHosts::buildHostList;
using MdnsHosts::MAX_HOSTS;

void test_mdns_hosts_setUp() {}
void test_mdns_hosts_tearDown() {}

void test_distinct_names_are_all_listed_in_order()
{
    const char* out[MAX_HOSTS];
    size_t n = buildHostList("kitchen", "l70-a1b2", "andromeda-a1b2", out);
    TEST_ASSERT_EQUAL(4, n);
    TEST_ASSERT_EQUAL_STRING("kitchen", out[0]);
    TEST_ASSERT_EQUAL_STRING("l70-a1b2", out[1]);
    TEST_ASSERT_EQUAL_STRING("andromeda-a1b2", out[2]);
    TEST_ASSERT_EQUAL_STRING("andromeda", out[3]);
}

void test_default_name_matches_model_uid_is_deduped()
{
    // A never-renamed device: primary == modelUid (both are getDefaultName()'s label).
    const char* out[MAX_HOSTS];
    size_t n = buildHostList("l70-a1b2", "l70-a1b2", "andromeda-a1b2", out);
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL_STRING("l70-a1b2", out[0]);
    TEST_ASSERT_EQUAL_STRING("andromeda-a1b2", out[1]);
    TEST_ASSERT_EQUAL_STRING("andromeda", out[2]);
}

void test_andromeda_mk0_collapses_model_and_andromeda_uid()
{
    // ANDROMEDA_MK0 (and the unresolvable-model fallback) both derive the same
    // "Andromeda-<uid>" name for getDefaultName() and getAndromedaMdnsHostname().
    const char* out[MAX_HOSTS];
    size_t n = buildHostList("andromeda-a1b2", "andromeda-a1b2", "andromeda-a1b2", out);
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_EQUAL_STRING("andromeda-a1b2", out[0]);
    TEST_ASSERT_EQUAL_STRING("andromeda", out[1]);
}

void test_empty_model_uid_is_skipped_not_listed_as_blank()
{
    const char* out[MAX_HOSTS];
    size_t n = buildHostList("kitchen", "", "andromeda-a1b2", out);
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL_STRING("kitchen", out[0]);
    TEST_ASSERT_EQUAL_STRING("andromeda-a1b2", out[1]);
    TEST_ASSERT_EQUAL_STRING("andromeda", out[2]);
}

void test_null_model_uid_is_skipped()
{
    const char* out[MAX_HOSTS];
    size_t n = buildHostList("kitchen", nullptr, "andromeda-a1b2", out);
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL_STRING("kitchen", out[0]);
    TEST_ASSERT_EQUAL_STRING("andromeda-a1b2", out[1]);
    TEST_ASSERT_EQUAL_STRING("andromeda", out[2]);
}

void run_test_mdns_hosts_tests()
{
    RUN_TEST(test_distinct_names_are_all_listed_in_order);
    RUN_TEST(test_default_name_matches_model_uid_is_deduped);
    RUN_TEST(test_andromeda_mk0_collapses_model_and_andromeda_uid);
    RUN_TEST(test_empty_model_uid_is_skipped_not_listed_as_blank);
    RUN_TEST(test_null_model_uid_is_skipped);
}
