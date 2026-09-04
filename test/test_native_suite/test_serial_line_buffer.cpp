#include <unity.h>

#include <cstring>
#include <string>

#include "serial-line-buffer.h"

void test_serial_line_buffer_setUp() {}
void test_serial_line_buffer_tearDown() {}

namespace
{
// Feeds a whole C string byte-by-byte, returning the last feed() result and
// filling outLine with whatever that last call produced (empty if false).
bool feedAll(SerialLineBuffer& buf, const char* input, char* outLine, size_t outLineSize)
{
    bool result = false;
    outLine[0] = '\0';
    for (const char* p = input; *p != '\0'; p++) result = buf.feed(*p, outLine, outLineSize);
    return result;
}
}  // namespace

void test_single_complete_line()
{
    SerialLineBuffer buf;
    char line[64];
    TEST_ASSERT_TRUE(feedAll(buf, "hello\n", line, sizeof(line)));
    TEST_ASSERT_EQUAL_STRING("hello", line);
}

void test_multiple_lines_in_one_burst()
{
    SerialLineBuffer buf;
    char first[64], second[64];
    const char* input = "first\nsecond\n";
    const char* p = input;
    while (!buf.feed(*p++, first, sizeof(first)));    // consumes "first\n"
    while (!buf.feed(*p++, second, sizeof(second)));  // consumes "second\n"
    TEST_ASSERT_EQUAL_STRING("first", first);
    TEST_ASSERT_EQUAL_STRING("second", second);
}

void test_line_fed_one_byte_per_call_still_completes()
{
    SerialLineBuffer buf;
    char line[64];
    TEST_ASSERT_FALSE(buf.feed('h', line, sizeof(line)));
    TEST_ASSERT_FALSE(buf.feed('i', line, sizeof(line)));
    TEST_ASSERT_TRUE(buf.feed('\n', line, sizeof(line)));
    TEST_ASSERT_EQUAL_STRING("hi", line);
}

void test_crlf_terminated_line_strips_the_cr()
{
    SerialLineBuffer buf;
    char line[64];
    TEST_ASSERT_TRUE(feedAll(buf, "hello\r\n", line, sizeof(line)));
    TEST_ASSERT_EQUAL_STRING("hello", line);
}

void test_empty_line_yields_empty_string()
{
    SerialLineBuffer buf;
    char line[64];
    line[0] = 'x';
    TEST_ASSERT_TRUE(buf.feed('\n', line, sizeof(line)));
    TEST_ASSERT_EQUAL_STRING("", line);
}

void test_exact_fit_line_boundary()
{
    SerialLineBuffer buf;
    char line[8];
    // outLineSize=8 -> room for 7 chars + NUL, exactly at the boundary.
    TEST_ASSERT_TRUE(feedAll(buf, "1234567\n", line, sizeof(line)));
    TEST_ASSERT_EQUAL_STRING("1234567", line);
}

// The critical bad-weather case: a truncated JSON can still parse into a
// *different* valid command, so an over-length line must be discarded, not
// handed on truncated.
void test_over_length_line_is_discarded_not_truncated()
{
    SerialLineBuffer buf;
    char line[512];
    std::string longLine(300, 'x');
    longLine += '\n';
    TEST_ASSERT_FALSE(feedAll(buf, longLine.c_str(), line, sizeof(line)));
}

void test_overflow_immediately_followed_by_a_valid_line_resyncs()
{
    SerialLineBuffer buf;
    char line[512];
    std::string longLine(300, 'x');
    longLine += '\n';
    feedAll(buf, longLine.c_str(), line, sizeof(line));

    // The next, well-formed line must parse cleanly - the buffer must not
    // still be "in" the discarded overflow.
    TEST_ASSERT_TRUE(feedAll(buf, "recovered\n", line, sizeof(line)));
    TEST_ASSERT_EQUAL_STRING("recovered", line);
}

void test_binary_garbage_mid_line_does_not_corrupt_the_next_good_line()
{
    SerialLineBuffer buf;
    char line[64];
    // NUL and other non-ASCII bytes mid-line: garbage in, garbage out for
    // that one line, but must not wedge the buffer for the next line.
    TEST_ASSERT_FALSE(buf.feed('a', line, sizeof(line)));
    TEST_ASSERT_FALSE(buf.feed('\0', line, sizeof(line)));
    TEST_ASSERT_FALSE(buf.feed((char)0xFF, line, sizeof(line)));
    TEST_ASSERT_TRUE(buf.feed('\n', line, sizeof(line)));

    TEST_ASSERT_TRUE(feedAll(buf, "clean\n", line, sizeof(line)));
    TEST_ASSERT_EQUAL_STRING("clean", line);
}

void run_test_serial_line_buffer_tests()
{
    RUN_TEST(test_single_complete_line);
    RUN_TEST(test_multiple_lines_in_one_burst);
    RUN_TEST(test_line_fed_one_byte_per_call_still_completes);
    RUN_TEST(test_crlf_terminated_line_strips_the_cr);
    RUN_TEST(test_empty_line_yields_empty_string);
    RUN_TEST(test_exact_fit_line_boundary);
    RUN_TEST(test_over_length_line_is_discarded_not_truncated);
    RUN_TEST(test_overflow_immediately_followed_by_a_valid_line_resyncs);
    RUN_TEST(test_binary_garbage_mid_line_does_not_corrupt_the_next_good_line);
}
