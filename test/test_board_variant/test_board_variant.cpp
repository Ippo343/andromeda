// Guards include/board-variant.h against platformio.ini drift.
//
// BOARD_VARIANT has to equal the PlatformIO env name for each hardware board,
// because OTA (#63) builds release assets named firmware-<env>-<tag>.bin and
// the device picks its download by matching BOARD_VARIANT. If someone renames
// an env, adds a board, or changes a -DESP32_* flag without updating the
// header, a deployed unit would silently stop finding its updates - and that
// isn't visible in a normal build, since each env only compiles its own arm.
#include <unity.h>

#include <fstream>
#include <sstream>
#include <string>

#include "board-variant.h"

void setUp() {}
void tearDown() {}

namespace
{
std::string readFile(const std::string& path)
{
    std::ifstream file(path);
    TEST_ASSERT_TRUE_MESSAGE(file.is_open(), ("Could not open " + path).c_str());
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

// Section headers only - lines that *start* with the pattern. platformio.ini
// mentions "[env:esp32_wroom]" inside comments too, which a plain search counts.
size_t countLinesStartingWith(const std::string& text, const std::string& prefix)
{
    size_t n = 0;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);)
    {
        size_t b = line.find_first_not_of(" \t");
        if (b != std::string::npos && line.compare(b, prefix.size(), prefix) == 0) ++n;
    }
    return n;
}
}  // namespace

void test_native_build_reports_native() { TEST_ASSERT_EQUAL_STRING("native", BOARD_VARIANT); }

void test_partition_layout_version_is_positive()
{
    TEST_ASSERT_GREATER_THAN_INT(0, PARTITION_LAYOUT_VERSION);
}

// The header names each board variant exactly as platformio.ini names the env
// and sets the matching -DESP32_* flag. Catches an env rename / flag change
// that the header wasn't told about.
void test_header_and_platformio_agree_on_every_board()
{
    std::string header = readFile("include/board-variant.h");
    std::string ini = readFile("platformio.ini");

    struct
    {
        const char* env;
        const char* flag;
    } boards[] = {
        {"esp32_wroom", "ESP32_WROOM"},
        {"esp32_s3_zero", "ESP32_S3"},
        {"esp32_c3_zero", "ESP32_C3"},
    };

    for (auto& b : boards)
    {
        std::string variantDef = std::string("#define BOARD_VARIANT \"") + b.env + "\"";
        TEST_ASSERT_TRUE_MESSAGE(header.find(variantDef) != std::string::npos,
                                 (variantDef + " missing from board-variant.h").c_str());
        TEST_ASSERT_TRUE_MESSAGE(
            header.find(std::string("defined(") + b.flag + ")") != std::string::npos,
            (std::string("board-variant.h must switch on ") + b.flag).c_str());

        TEST_ASSERT_TRUE_MESSAGE(
            ini.find(std::string("[env:") + b.env + "]") != std::string::npos,
            (std::string("[env:") + b.env + "] missing from platformio.ini").c_str());
        TEST_ASSERT_TRUE_MESSAGE(
            ini.find(std::string("-D") + b.flag) != std::string::npos,
            (std::string("-D") + b.flag + " missing from platformio.ini").c_str());
    }

    // No fourth hardware board slipped in without a header arm + a test row.
    TEST_ASSERT_EQUAL_MESSAGE(
        3, countLinesStartingWith(ini, "[env:esp32_"),
        "hardware env count changed - add a BOARD_VARIANT arm and a row above");
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_native_build_reports_native);
    RUN_TEST(test_partition_layout_version_is_positive);
    RUN_TEST(test_header_and_platformio_agree_on_every_board);
    return UNITY_END();
}
