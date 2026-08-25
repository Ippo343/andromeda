// Static-analysis test, not a build/runtime one: comms.cpp only serves files
// it explicitly registers via STATIC_FILE_ROUTE/server.on (there's no
// LittleFS catch-all - see comms.cpp), so a <script>/<link>/<a> tag added to
// data/index.html without a matching route silently 404s in the browser.
// That exact gap shipped once (the effect-selection-ui branch added
// controls-logic.js's <script> tag but not its route) with every layer of
// the real test suite green, because nothing crosses the boundary between
// "what index.html references" and "what comms.cpp serves" - this test
// exists purely to close that gap by diffing the two as plain text, without
// compiling or linking either file.
#include <unity.h>

#include <cstdio>
#include <fstream>
#include <regex>
#include <set>
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

// Local-asset references (src=/href=) from data/index.html, normalized to
// the leading-"/" form comms.cpp registers routes with. External links
// (http(s)://) and in-page anchors (#...) are not server routes, so skip them.
std::set<std::string> referencedLocalPaths(const std::string& html)
{
    std::set<std::string> paths;
    std::regex attrRe(R"RE((?:src|href)="([^"]+)")RE");
    for (auto it = std::sregex_iterator(html.begin(), html.end(), attrRe);
         it != std::sregex_iterator(); ++it)
    {
        std::string value = (*it)[1].str();
        if (value.empty() || value[0] == '#') continue;
        if (value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0) continue;
        if (value[0] != '/') value = "/" + value;
        paths.insert(value);
    }
    return paths;
}

// Paths comms.cpp registers a GET handler for, whether through the
// STATIC_FILE_ROUTE macro or a direct server.on(...) call (e.g. "/", "/wifi").
std::set<std::string> registeredRoutes(const std::string& commsCpp)
{
    std::set<std::string> routes;
    std::regex routeRe(R"RE((?:STATIC_FILE_ROUTE|server\.on)\(\s*"([^"]+)")RE");
    for (auto it = std::sregex_iterator(commsCpp.begin(), commsCpp.end(), routeRe);
         it != std::sregex_iterator(); ++it)
        routes.insert((*it)[1].str());
    return routes;
}
}  // namespace

void test_every_index_html_local_asset_has_a_registered_route()
{
    std::set<std::string> referenced = referencedLocalPaths(readFile("data/index.html"));
    std::set<std::string> registered = registeredRoutes(readFile("src/comms.cpp"));

    for (const std::string& path : referenced)
    {
        std::string message = path +
                              " is referenced by data/index.html but comms.cpp has no "
                              "route for it - it will 404 in the browser";
        TEST_ASSERT_TRUE_MESSAGE(registered.count(path) > 0, message.c_str());
    }
}

int main(int argc, char** argv)
{
    UNITY_BEGIN();

    RUN_TEST(test_every_index_html_local_asset_has_a_registered_route);

    return UNITY_END();
}
