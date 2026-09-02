#include <unity.h>

#include <limits>

#include "comms-utils.h"

void setUp() {}
void tearDown() {}

void test_scan_cache_invalid_when_no_scan_completed()
{
    TEST_ASSERT_FALSE(isScanCacheValid(false, 0, 1000, 30000));
}

void test_scan_cache_valid_within_window()
{
    TEST_ASSERT_TRUE(isScanCacheValid(true, 1000, 1000 + 29999, 30000));
}

void test_scan_cache_invalid_once_expired()
{
    TEST_ASSERT_FALSE(isScanCacheValid(true, 1000, 1000 + 30000, 30000));
    TEST_ASSERT_FALSE(isScanCacheValid(true, 1000, 1000 + 40000, 30000));
}

void test_scan_cache_valid_at_exact_scan_time()
{
    TEST_ASSERT_TRUE(isScanCacheValid(true, 5000, 5000, 30000));
}

// ---------------------------------------------------------------------------
// originMatchesHost - the same-origin check behind isCrossOriginPost() and
// the WebSocket handshake guard
// ---------------------------------------------------------------------------

void test_origin_matches_host_accepts_the_devices_own_origin()
{
    TEST_ASSERT_TRUE(originMatchesHost("http://andromeda-ab12.local", "andromeda-ab12.local"));
    TEST_ASSERT_TRUE(originMatchesHost("https://andromeda-ab12.local", "andromeda-ab12.local"));
    TEST_ASSERT_TRUE(originMatchesHost("http://192.168.1.50", "192.168.1.50"));
    // Host carrying an explicit port must match the origin's port too.
    TEST_ASSERT_TRUE(originMatchesHost("http://192.168.1.50:8080", "192.168.1.50:8080"));
}

void test_origin_matches_host_rejects_suffix_and_port_tricks()
{
    // The substring check this replaced accepted all of these.
    TEST_ASSERT_FALSE(
        originMatchesHost("http://andromeda-ab12.local.evil.example", "andromeda-ab12.local"));
    TEST_ASSERT_FALSE(
        originMatchesHost("http://evil.example/?andromeda-ab12.local", "andromeda-ab12.local"));
    TEST_ASSERT_FALSE(originMatchesHost("http://192.168.1.50:8080", "192.168.1.50"));
    TEST_ASSERT_FALSE(originMatchesHost("http://192.168.1.500", "192.168.1.50"));
    TEST_ASSERT_FALSE(originMatchesHost("http://evil.example", "andromeda-ab12.local"));
}

void test_origin_matches_host_rejects_empty_or_missing()
{
    TEST_ASSERT_FALSE(originMatchesHost("", "andromeda-ab12.local"));
    TEST_ASSERT_FALSE(originMatchesHost(nullptr, "andromeda-ab12.local"));
    TEST_ASSERT_FALSE(originMatchesHost("http://andromeda-ab12.local", ""));
    TEST_ASSERT_FALSE(originMatchesHost("http://andromeda-ab12.local", nullptr));
}

void test_scan_cache_handles_millis_wraparound()
{
    // millis() overflows back to a small value after ~49.7 days; unsigned
    // subtraction must still produce the correct (small, positive) elapsed
    // time rather than a huge one.
    unsigned long lastScanTime = std::numeric_limits<unsigned long>::max() - 100;
    unsigned long now = 50;  // wrapped around: 100 + 50 = 150ms actually elapsed
    TEST_ASSERT_TRUE(isScanCacheValid(true, lastScanTime, now, 30000));
}

int main(int argc, char** argv)
{
    UNITY_BEGIN();

    RUN_TEST(test_scan_cache_invalid_when_no_scan_completed);
    RUN_TEST(test_scan_cache_valid_within_window);
    RUN_TEST(test_scan_cache_invalid_once_expired);
    RUN_TEST(test_scan_cache_valid_at_exact_scan_time);
    RUN_TEST(test_scan_cache_handles_millis_wraparound);

    RUN_TEST(test_origin_matches_host_accepts_the_devices_own_origin);
    RUN_TEST(test_origin_matches_host_rejects_suffix_and_port_tricks);
    RUN_TEST(test_origin_matches_host_rejects_empty_or_missing);

    return UNITY_END();
}
