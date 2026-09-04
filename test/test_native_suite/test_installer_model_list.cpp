// Closes the browser<->firmware seam for the web installer's "set model"
// step (#105/#187): web-installer/js/models.js is a hand-maintained mirror of
// include/geometry/model_config.h's registered, shippable models, and
// nothing in the browser can verify the firmware actually accepts what it
// offers. This is a static/plain-text check, same approach as
// test_web_installer.cpp and test_static_assets.cpp - no browser runtime, no
// PlatformIO extra step, just reading the real files.
//
// What's guarded:
//   - every model models.js offers builds into the exact JSON line
//     WsCommandParser::parse() (the same parser the WebSocket handler and
//     the new USB-serial reader both call) accepts as a MODEL command with
//     that id - a typo'd template or stale id fails here, not in a browser.
//   - every id models.js offers resolves via getModelConfig() - the same
//     check mission-control.cpp's MODEL handler makes before persisting one,
//     so an offered model the firmware would silently reject (e.g. a
//     GRID_TEST_DEVICE-style entry with no registry row) fails here instead.
//   - every registered, shippable model (Andromeda/L-Series family, i.e. not
//     a TEST_DEVICES-family rig) is offered - a new model added to the
//     firmware but never surfaced in the installer fails here.
#include <unity.h>

#include <cstdlib>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "geometry/geometry.h"
#include "geometry/model_registry.h"
#include "ws-command-parser.h"

void test_installer_model_list_setUp() {}
void test_installer_model_list_tearDown() {}

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

// Extracts {id, label} pairs from web-installer/js/models.js's MODELS array,
// e.g. `{ id: 0x0300, label: 'L70 MK1' },` -> (0x0300, "L70 MK1").
std::vector<std::pair<uint16_t, std::string>> parseModelsJs(const std::string& src)
{
    std::vector<std::pair<uint16_t, std::string>> out;
    std::regex entryRe(R"(\{\s*id:\s*(0x[0-9A-Fa-f]+),\s*label:\s*'([^']+)'\s*\})");
    for (auto it = std::sregex_iterator(src.begin(), src.end(), entryRe);
         it != std::sregex_iterator(); ++it)
    {
        uint16_t id = static_cast<uint16_t>(strtoul((*it)[1].str().c_str(), nullptr, 16));
        out.emplace_back(id, (*it)[2].str());
    }
    return out;
}
}  // namespace

void test_models_js_has_at_least_one_entry()
{
    auto models = parseModelsJs(readFile("web-installer/js/models.js"));
    TEST_ASSERT_TRUE_MESSAGE(models.size() > 0,
                             "web-installer/js/models.js's MODELS array didn't parse - update "
                             "this test's regex if the file's shape changed");
}

void test_every_offered_model_builds_a_command_the_firmware_accepts()
{
    auto models = parseModelsJs(readFile("web-installer/js/models.js"));
    for (const auto& [id, label] : models)
    {
        std::string json = "{\"type\":\"model\",\"id\":" + std::to_string(id) + "}";
        Command command;
        bool parsed = WsCommandParser::parse(json.c_str(), command);
        TEST_ASSERT_TRUE_MESSAGE(parsed, label.c_str());
        TEST_ASSERT_TRUE_MESSAGE(command.type == CommandType::MODEL, label.c_str());
        TEST_ASSERT_EQUAL_MESSAGE(id, command.modelId, label.c_str());
    }
}

void test_every_offered_model_resolves_to_a_real_registry_entry()
{
    auto models = parseModelsJs(readFile("web-installer/js/models.js"));
    for (const auto& [id, label] : models)
        TEST_ASSERT_NOT_NULL_MESSAGE(getModelConfig(static_cast<ModelId>(id)), label.c_str());
}

// Bad weather: a shippable model added to the firmware registry but never
// surfaced in models.js would leave a customer unable to select their own
// hardware from the installer, with nothing anywhere flagging the gap.
void test_every_shippable_registered_model_is_offered()
{
    auto models = parseModelsJs(readFile("web-installer/js/models.js"));
    std::set<uint16_t> offered;
    for (const auto& [id, label] : models) offered.insert(id);

    for (size_t i = 0; i < NUM_MODELS; i++)
    {
        const ModelConfig* config = MODEL_REGISTRY[i];
        bool shippable =
            config->isInFamily(FamilyID::ANDROMEDA) || config->isInFamily(FamilyID::L_SERIES);
        if (!shippable) continue;  // test rigs are deliberately excluded

        TEST_ASSERT_TRUE_MESSAGE(offered.count(static_cast<uint16_t>(config->id)) > 0,
                                 config->name);
    }
}

void run_test_installer_model_list_tests()
{
    RUN_TEST(test_models_js_has_at_least_one_entry);
    RUN_TEST(test_every_offered_model_builds_a_command_the_firmware_accepts);
    RUN_TEST(test_every_offered_model_resolves_to_a_real_registry_entry);
    RUN_TEST(test_every_shippable_registered_model_is_offered);
}
