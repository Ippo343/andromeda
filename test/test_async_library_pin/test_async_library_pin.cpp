// Static-analysis test, not a build/runtime one: reads platformio.ini as
// plain text and asserts the async web-server / TCP stack stays on the
// maintained ESP32Async pair, exact-pinned.
//
// Issue #107 was a crash in the accept path of the stale AsyncTCP that
// lacamera/ESPAsyncWebServer#3.1.0 vendored; the fix was to swap to
// ESP32Async/AsyncTCP + ESP32Async/ESPAsyncWebServer, whose accept
// trampoline NULL-checks the pcb. That guard lives in third-party source we
// pin by version - the paired build-scripts/check_asynctcp_accept_guard.py
// verifies the guard itself is present in the *resolved* library on every
// hardware build; this test is the cheap half that runs in the native suite
// (no libdeps needed) and fails the moment platformio.ini drifts back toward
// a stale fork, a caret/range spec, or the retired local monkey-patch.
#include <unity.h>

#include <fstream>
#include <regex>
#include <sstream>
#include <string>

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

// platformio.ini with every ';'-comment line dropped, so a comment that
// mentions an old fork by name (to explain the history) doesn't trip the
// "no stale fork" check - only real dependency / script lines count.
std::string stripComments(const std::string& ini)
{
    std::string out;
    std::istringstream in(ini);
    for (std::string line; std::getline(in, line);)
    {
        size_t firstNonWs = line.find_first_not_of(" \t");
        if (firstNonWs != std::string::npos && line[firstNonWs] == ';') continue;
        out += line;
        out += '\n';
    }
    return out;
}

bool contains(const std::string& haystack, const std::regex& re)
{
    return std::regex_search(haystack, re);
}
}  // namespace

// Both halves of the maintained pair are present and pinned to an exact
// x.y.z (not "@^", not "@~", not a bare git URL - an earlier fork
// re-downloaded on every build with a range spec, and a git URL can't be
// audited by version).
void test_async_libs_are_exact_pinned_esp32async_pair()
{
    std::string ini = readFile("platformio.ini");

    TEST_ASSERT_TRUE_MESSAGE(
        contains(ini, std::regex(R"(ESP32Async/AsyncTCP@\d+\.\d+\.\d+)", std::regex::icase)),
        "platformio.ini must pin ESP32Async/AsyncTCP@<exact x.y.z>");
    TEST_ASSERT_TRUE_MESSAGE(
        contains(ini,
                 std::regex(R"(ESP32Async/ESPAsyncWebServer@\d+\.\d+\.\d+)", std::regex::icase)),
        "platformio.ini must pin ESP32Async/ESPAsyncWebServer@<exact x.y.z>");

    TEST_ASSERT_FALSE_MESSAGE(
        contains(ini, std::regex(R"(ESP32Async/(?:AsyncTCP|ESPAsyncWebServer)@[\^~])",
                                 std::regex::icase)),
        "the async libs must be exact-pinned, not a caret/tilde range");
}

// No path back to the stale forks the swap moved off of, and no reference to
// the retired build-time monkey-patch from the abandoned local-fix branch.
void test_no_stale_async_fork_or_monkey_patch()
{
    std::string ini = stripComments(readFile("platformio.ini"));

    const char* forbidden[] = {
        "lacamera",  // the git-pinned fork #107 was stuck on
        "ESPAsyncWebServer-esphome",
        "AsyncTCP-esphome",  // esphome vendored copies
        "me-no-dev/ESPAsyncWebServer",
        "me-no-dev/AsyncTCP",
        "patch_asynctcp",  // build-scripts/patch_asynctcp_accept.py (issue #107 option 2)
    };
    for (const char* needle : forbidden)
    {
        std::string msg = std::string("platformio.ini still references '") + needle +
                          "' - the #107 fix is the ESP32Async library swap, nothing else";
        TEST_ASSERT_TRUE_MESSAGE(ini.find(needle) == std::string::npos, msg.c_str());
    }
}

// The accept-guard script the swap relies on stays wired as a post-action.
void test_accept_guard_script_is_wired()
{
    std::string ini = readFile("platformio.ini");
    TEST_ASSERT_TRUE_MESSAGE(
        ini.find("post:build-scripts/check_asynctcp_accept_guard.py") != std::string::npos,
        "check_asynctcp_accept_guard.py must stay wired as a post: extra_script");
}

// ArduinoJson stays on v6: ws-state-builder.h uses StaticJsonDocument, which
// v7 removed. The new ESPAsyncWebServer declares an ArduinoJson dependency of
// its own (for the AsyncJson.h helper we don't include) - make sure that
// can't quietly pull the pin forward.
void test_arduinojson_still_pinned_v6()
{
    std::string ini = readFile("platformio.ini");
    TEST_ASSERT_TRUE_MESSAGE(contains(ini, std::regex(R"(bblanchon/ArduinoJson@\^6\.)")),
                             "ArduinoJson must stay pinned to ^6 (v7 dropped StaticJsonDocument)");
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_async_libs_are_exact_pinned_esp32async_pair);
    RUN_TEST(test_no_stale_async_fork_or_monkey_patch);
    RUN_TEST(test_accept_guard_script_is_wired);
    RUN_TEST(test_arduinojson_still_pinned_v6);
    return UNITY_END();
}
