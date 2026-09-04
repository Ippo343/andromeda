// Static-analysis test, not a build/runtime one: parses
// partitions/andromeda_4mb.csv and pins every partition's offset and size.
//
// Once units ship with OTA (#63) this table is frozen for the fleet - it
// lives at flash offset 0x8000, outside every OTA-writable partition, so a
// later change means physically reflashing every deployed unit over USB.
// PARTITION_LAYOUT_VERSION (include/board-variant.h) is the deliberate
// "yes, I know that" switch; this test fails the moment the CSV drifts from
// the values recorded for the current version, so an accidental edit - or a
// real change without bumping the constant - can't reach CI unnoticed.
#include <unity.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "board-variant.h"

#if PARTITION_LAYOUT_VERSION != 1
#error \
    "PARTITION_LAYOUT_VERSION changed - update the expected table below and the CSV comment, then bump this guard. Remember every deployed unit needs a USB reflash."
#endif

void test_partition_layout_setUp() {}
void test_partition_layout_tearDown() {}

namespace
{
constexpr uint32_t FLASH_4MB = 0x400000;
constexpr uint32_t APP_ALIGN = 0x10000;  // esp-idf requires app partitions 64 KiB-aligned

struct Part
{
    std::string name;
    std::string type;
    std::string subtype;
    uint32_t offset;
    uint32_t size;
};

// The single source of truth this test guards. Keep in lockstep with
// partitions/andromeda_4mb.csv and its header comment.
const std::vector<Part> kExpected = {
    {"nvs", "data", "nvs", 0x9000, 0x5000},
    {"otadata", "data", "ota", 0xe000, 0x2000},
    {"app0", "app", "ota_0", 0x10000, 0x1A0000},
    {"app1", "app", "ota_1", 0x1B0000, 0x1A0000},
    {"spiffs", "data", "spiffs", 0x350000, 0xA0000},
    {"coredump", "data", "coredump", 0x3F0000, 0x10000},
};

std::string trim(const std::string& s)
{
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

uint32_t parseNum(const std::string& s)
{
    return static_cast<uint32_t>(std::stoul(trim(s), nullptr, 0));
}

std::vector<Part> readCsv(const std::string& path)
{
    std::ifstream file(path);
    TEST_ASSERT_TRUE_MESSAGE(file.is_open(), ("Could not open " + path).c_str());

    std::vector<Part> out;
    for (std::string line; std::getline(file, line);)
    {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;

        std::vector<std::string> cols;
        std::istringstream ss(t);
        for (std::string col; std::getline(ss, col, ',');) cols.push_back(trim(col));

        TEST_ASSERT_TRUE_MESSAGE(cols.size() >= 5, ("Malformed partition line: " + line).c_str());
        out.push_back({cols[0], cols[1], cols[2], parseNum(cols[3]), parseNum(cols[4])});
    }
    return out;
}
}  // namespace

void test_partition_table_matches_expected_layout()
{
    std::vector<Part> got = readCsv("partitions/andromeda_4mb.csv");

    TEST_ASSERT_EQUAL_MESSAGE(kExpected.size(), got.size(),
                              "partition count changed - see PARTITION_LAYOUT_VERSION");

    for (size_t i = 0; i < kExpected.size(); ++i)
    {
        const Part& e = kExpected[i];
        const Part& g = got[i];
        std::string where = " (partition '" + e.name + "')";

        TEST_ASSERT_EQUAL_STRING_MESSAGE(e.name.c_str(), g.name.c_str(),
                                         ("name mismatch" + where).c_str());
        TEST_ASSERT_EQUAL_STRING_MESSAGE(e.type.c_str(), g.type.c_str(),
                                         ("type mismatch" + where).c_str());
        TEST_ASSERT_EQUAL_STRING_MESSAGE(e.subtype.c_str(), g.subtype.c_str(),
                                         ("subtype mismatch" + where).c_str());
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(e.offset, g.offset, ("offset mismatch" + where).c_str());
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(e.size, g.size, ("size mismatch" + where).c_str());
    }
}

// Independent sanity checks on whatever the CSV actually says, so a matching
// edit to both this test's table and the CSV still can't produce a physically
// invalid layout.
void test_partition_table_is_physically_valid()
{
    std::vector<Part> parts = readCsv("partitions/andromeda_4mb.csv");

    uint32_t prevEnd = 0;
    for (const Part& p : parts)
    {
        TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(prevEnd, p.offset,
                                             ("partitions overlap at '" + p.name + "'").c_str());
        if (p.type == "app")
            TEST_ASSERT_EQUAL_HEX32_MESSAGE(
                0, p.offset % APP_ALIGN,
                ("app partition '" + p.name + "' not 64 KiB-aligned").c_str());
        prevEnd = p.offset + p.size;
    }

    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(FLASH_4MB, prevEnd,
                                      "partition table runs past the end of a 4 MB chip");

    // The two OTA slots must be the same size, or the smaller one caps every
    // future firmware image without it being obvious why.
    uint32_t app0 = 0, app1 = 0;
    for (const Part& p : parts)
    {
        if (p.subtype == "ota_0") app0 = p.size;
        if (p.subtype == "ota_1") app1 = p.size;
    }
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, app0, "no ota_0 partition");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(app0, app1, "the two OTA app slots differ in size");
}

void run_test_partition_layout_tests()
{
    RUN_TEST(test_partition_table_matches_expected_layout);
    RUN_TEST(test_partition_table_is_physically_valid);
}
