#include <unity.h>

#include <cstring>
#include <string>

#include "log-ring.h"

// Resets the shared LogRing singleton to empty between cases via the
// UNIT_TEST-guarded friend declaration in log-ring.h - the ctor is private, so
// this mirrors PerformanceMonitorTestAccess in test_perf_monitor.
class LogRingTestAccess
{
   public:
    static void reset(LogRing& r)
    {
        std::memset(r._buf, 0, LogRing::CAP);
        r._head = 0;
        r._full = false;
    }
};

// Tracks heap activity so a test can assert LogRing does no allocation. Global
// new/delete overrides are process-wide but harmless: only the zero-alloc test
// reads the counter, and Unity itself does no per-assert heap work here.
static bool g_allocTrackingOn = false;
static size_t g_allocCount = 0;

void* operator new(std::size_t n)
{
    if (g_allocTrackingOn) ++g_allocCount;
    return std::malloc(n ? n : 1);
}
void* operator new[](std::size_t n)
{
    if (g_allocTrackingOn) ++g_allocCount;
    return std::malloc(n ? n : 1);
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

void setUp() { LogRingTestAccess::reset(LogRing::Instance()); }
void tearDown() { g_allocTrackingOn = false; }

namespace
{
void writeStr(const char* s)
{
    LogRing::Instance().write(reinterpret_cast<const uint8_t*>(s), std::strlen(s));
}

std::string snap()
{
    char buf[LogRing::CAP];
    size_t n = LogRing::Instance().snapshot(buf, sizeof(buf));
    return std::string(buf, n);
}
}  // namespace

void test_empty_ring_snapshots_nothing()
{
    char buf[16];
    TEST_ASSERT_EQUAL_UINT32(0, LogRing::Instance().snapshot(buf, sizeof(buf)));
}

void test_short_write_round_trips_in_order()
{
    writeStr("hello\n");
    writeStr("world\n");
    TEST_ASSERT_EQUAL_STRING("hello\nworld\n", snap().c_str());
}

void test_write_exactly_cap_keeps_everything()
{
    std::string in(LogRing::CAP, 'x');
    LogRing::Instance().write(reinterpret_cast<const uint8_t*>(in.data()), in.size());
    std::string out = snap();
    TEST_ASSERT_EQUAL_UINT32(LogRing::CAP, out.size());
    TEST_ASSERT_TRUE(out == in);
}

void test_wrap_discards_oldest_bytes()
{
    // CAP-1 'a's, then "0123456789" - the 10-byte tail pushes 9 'a's out.
    std::string a(LogRing::CAP - 1, 'a');
    LogRing::Instance().write(reinterpret_cast<const uint8_t*>(a.data()), a.size());
    writeStr("0123456789");

    std::string out = snap();
    TEST_ASSERT_EQUAL_UINT32(LogRing::CAP, out.size());
    TEST_ASSERT_EQUAL_UINT32(LogRing::CAP - 10, out.find_first_not_of('a'));
    TEST_ASSERT_EQUAL_STRING("0123456789", out.substr(LogRing::CAP - 10).c_str());
}

void test_write_larger_than_cap_keeps_only_trailing_cap_bytes()
{
    // Bounded work: a single 10x-CAP write still leaves exactly CAP bytes, and
    // they are the newest ones.
    std::string big;
    big.reserve(LogRing::CAP * 10);
    for (size_t i = 0; i < LogRing::CAP * 10; ++i) big.push_back(static_cast<char>('A' + (i % 26)));
    LogRing::Instance().write(reinterpret_cast<const uint8_t*>(big.data()), big.size());

    std::string out = snap();
    TEST_ASSERT_EQUAL_UINT32(LogRing::CAP, out.size());
    TEST_ASSERT_TRUE(out == big.substr(big.size() - LogRing::CAP));
}

void test_repeated_writes_over_cap_keep_newest()
{
    // Same as above but accumulated across many small writes.
    std::string all;
    for (int i = 0; i < 5000; ++i)
    {
        char line[32];
        int m = std::snprintf(line, sizeof(line), "line-%d\n", i);
        LogRing::Instance().write(reinterpret_cast<const uint8_t*>(line), static_cast<size_t>(m));
        all.append(line, static_cast<size_t>(m));
    }
    std::string out = snap();
    TEST_ASSERT_EQUAL_UINT32(LogRing::CAP, out.size());
    TEST_ASSERT_TRUE(out == all.substr(all.size() - LogRing::CAP));
}

void test_snapshot_into_small_buffer_returns_newest_tail()
{
    writeStr("abcdefghij");
    char buf[4];
    size_t n = LogRing::Instance().snapshot(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT32(4, n);
    TEST_ASSERT_EQUAL_STRING("ghij", std::string(buf, n).c_str());
}

void test_snapshot_small_buffer_after_wrap()
{
    std::string a(LogRing::CAP, 'a');
    LogRing::Instance().write(reinterpret_cast<const uint8_t*>(a.data()), a.size());
    writeStr("XYZ");  // ring now ends ...aaaXYZ, wrapped

    char buf[5];
    size_t n = LogRing::Instance().snapshot(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT32(5, n);
    TEST_ASSERT_EQUAL_STRING("aaXYZ", std::string(buf, n).c_str());
}

void test_zero_bytes_and_null_are_noops()
{
    writeStr("keep\n");
    LogRing::Instance().write(nullptr, 10);
    LogRing::Instance().write(reinterpret_cast<const uint8_t*>("x"), 0);
    TEST_ASSERT_EQUAL_STRING("keep\n", snap().c_str());
}

void test_writes_and_snapshot_do_not_allocate()
{
    std::string chunk(500, 'z');
    g_allocCount = 0;
    g_allocTrackingOn = true;
    for (int i = 0; i < 100; ++i)
    {
        LogRing::Instance().write(reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size());
        char buf[LogRing::CAP];
        LogRing::Instance().snapshot(buf, sizeof(buf));
    }
    g_allocTrackingOn = false;
    TEST_ASSERT_EQUAL_UINT32(0, g_allocCount);
}

// Compile-time guard: the buffer is an inline fixed array, never a heap block.
static_assert(sizeof(LogRing) >= LogRing::CAP, "LogRing must own its buffer inline");

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_ring_snapshots_nothing);
    RUN_TEST(test_short_write_round_trips_in_order);
    RUN_TEST(test_write_exactly_cap_keeps_everything);
    RUN_TEST(test_wrap_discards_oldest_bytes);
    RUN_TEST(test_write_larger_than_cap_keeps_only_trailing_cap_bytes);
    RUN_TEST(test_repeated_writes_over_cap_keep_newest);
    RUN_TEST(test_snapshot_into_small_buffer_returns_newest_tail);
    RUN_TEST(test_snapshot_small_buffer_after_wrap);
    RUN_TEST(test_zero_bytes_and_null_are_noops);
    RUN_TEST(test_writes_and_snapshot_do_not_allocate);
    return UNITY_END();
}
