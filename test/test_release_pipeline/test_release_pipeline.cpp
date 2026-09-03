// Static-analysis test, not a build/runtime one: reads the release pipeline's
// own files as plain text and asserts the gates that stop a broken build from
// reaching the fleet are still wired.
//
// Everything here guards the path that publishes OTA firmware, where a silent
// failure is worst: a tag push builds and publishes 10 assets (3 firmware +
// 3 littlefs + 3 flashparts zips + manifest.json, #162) that every device on
// that channel is then offered. The audit found five ways that path could
// ship or perpetuate a bad build, three of them with no error anywhere:
//
//   R1  test.yml didn't trigger on tags, so a release ran no tests at all.
//   R2  version codes are commit counts, which are only monotonic along one
//       line of history - a tag off a side branch produces a code the fleet
//       already exceeds, so the update is never offered and nothing errors.
//   R3  the release was published live, relying on dist/*'s alphabetical
//       ordering to upload manifest.json last. Accidental, not enforced.
//   R4  the #107 accept-guard returned "pass" when its glob matched no files.
//   R5  CI never built the LittleFS image, so a broken data/ first failed at
//       release time, after the firmware halves were staged.
//
// These are text assertions on purpose: they need no libdeps, no toolchain and
// no network, so they run in the native suite alongside test_async_library_pin
// and test_static_assets, which use the same approach.
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

// A workflow with every '#'-comment line dropped. The workflows explain their
// own history in comments (which name the very things some checks forbid), so
// only real YAML lines may count.
std::string stripComments(const std::string& yaml)
{
    std::string out;
    std::istringstream in(yaml);
    for (std::string line; std::getline(in, line);)
    {
        size_t firstNonWs = line.find_first_not_of(" \t");
        if (firstNonWs != std::string::npos && line[firstNonWs] == '#') continue;
        out += line;
        out += '\n';
    }
    return out;
}

bool contains(const std::string& haystack, const std::string& re)
{
    return std::regex_search(haystack, std::regex(re));
}

// Offset of the first line whose content *starts with* `command`, ignoring
// leading whitespace. Plain find() is not good enough for ordering checks: the
// workflows mention their own commands in trailing comments (e.g.
// `contents: write # gh release create / upload`), and stripComments only
// drops whole-line comments, so a naive search matches the comment and reports
// a step as running far earlier than it does.
size_t findCommandLine(const std::string& yaml, const std::string& command)
{
    std::istringstream in(yaml);
    size_t offset = 0;
    for (std::string line; std::getline(in, line);)
    {
        size_t firstNonWs = line.find_first_not_of(" \t");
        if (firstNonWs != std::string::npos &&
            line.compare(firstNonWs, command.size(), command) == 0)
            return offset;
        offset += line.size() + 1;
    }
    return std::string::npos;
}

// The subtype the data partition carries in the ESP-IDF table. Still "spiffs"
// even though the image is LittleFS - that is what the bootloader and esptool
// understand, and what the CSV declares.
const char* const _DATA_SUBTYPE_LITERAL = "spiffs";
}  // namespace

// --- R1: a tag push must run the suite ------------------------------------

// Good weather: the fix ADDS a tag trigger; it must not take away the two
// events the suite already runs on. Losing either would mean PRs (or main)
// stop being tested, which is a worse hole than the one being closed.
void test_test_workflow_keeps_its_existing_triggers()
{
    std::string yaml = stripComments(readFile(".github/workflows/test.yml"));

    TEST_ASSERT_TRUE_MESSAGE(contains(yaml, R"(branches:\s*\[\s*main\s*\])"),
                             "test.yml must still run on pushes to main");
    TEST_ASSERT_TRUE_MESSAGE(contains(yaml, R"(pull_request:)"),
                             "test.yml must still run on pull_request");
}

// Bad weather: the release job must not be reachable without the suite. Either
// form is accepted - a `needs:` on an in-workflow test job, or a `uses:` of
// test.yml as a reusable workflow - but one of them has to be there, so a
// future edit that drops the gate fails here instead of shipping untested
// firmware to every device on the channel.
void test_release_job_is_gated_on_the_test_suite()
{
    std::string yaml = stripComments(readFile(".github/workflows/release.yml"));

    const bool gated =
        contains(yaml, R"(needs:)") || contains(yaml, R"(uses:\s*\./\.github/workflows/test\.yml)");
    TEST_ASSERT_TRUE_MESSAGE(
        gated,
        "release.yml must gate publishing on the test suite (a `needs:` on a test job, or "
        "`uses: ./.github/workflows/test.yml`) - a tag push must never publish untested firmware");
}

// --- R2: version codes must be checked for monotonicity -------------------

// Bad weather: `git rev-list --count` is only monotonic along one line of
// history. A tag cut off an older commit or a side branch yields a code the
// fleet already exceeds, so OTA's `latest.versionCode > FIRMWARE_VERSION_CODE`
// is false and the release is silently never offered - the failure mode that
// leaves a fleet stranded on a broken build. The release must refuse to
// publish before it builds anything.
void test_release_checks_version_code_is_newer_than_the_published_one()
{
    std::string yaml = stripComments(readFile(".github/workflows/release.yml"));

    TEST_ASSERT_TRUE_MESSAGE(
        yaml.find("build-scripts/check_version_code.py") != std::string::npos,
        "release.yml must run check_version_code.py - a tag whose version code is not strictly "
        "greater than the newest published release's would ship an update the fleet can never see");

    // Fail fast: the guard is worthless if it runs after this job has already
    // downloaded/staged the built images, and worse than worthless after the
    // publish step. The actual build lives in a separate `test` job this one
    // depends on (needs: test, checked elsewhere) - #162 changed the release
    // job from building its own images to downloading test.yml's, so there's
    // no in-job `pio run` left to order against here.
    const size_t guardAt = findCommandLine(yaml, "python build-scripts/check_version_code.py");
    const size_t downloadAt = yaml.find("actions/download-artifact");
    const size_t publishAt = findCommandLine(yaml, "gh release create");
    TEST_ASSERT_TRUE_MESSAGE(guardAt != std::string::npos,
                             "the version-code guard must be invoked as its own command");
    TEST_ASSERT_TRUE_MESSAGE(downloadAt != std::string::npos && publishAt != std::string::npos,
                             "release.yml must still download the built images and publish");
    TEST_ASSERT_TRUE_MESSAGE(guardAt < downloadAt,
                             "the version-code guard must run before staging any build output");
    TEST_ASSERT_TRUE_MESSAGE(guardAt < publishAt,
                             "the version-code guard must run before the publish step");
}

// --- R3: publish atomically, don't rely on glob ordering ------------------

// Bad weather: the release used to be created live with `dist/*`, so a partial
// upload could expose a manifest.json pointing at assets that 404. It only
// worked because 'f' < 'l' < 'm' put the manifest last - an accident a rename
// would silently undo. Create as a draft, verify, then un-draft.
void test_release_is_published_as_a_verified_draft()
{
    std::string yaml = stripComments(readFile(".github/workflows/release.yml"));

    // `--draft` is set via the FLAGS variable the create line expands, so this
    // checks for the flag anywhere in the create step rather than on the same
    // line as the command.
    TEST_ASSERT_TRUE_MESSAGE(
        yaml.find("--draft") != std::string::npos,
        "release.yml must create the release as a draft, so a partially-uploaded release is "
        "never visible to a device polling mid-upload");
    TEST_ASSERT_TRUE_MESSAGE(
        contains(yaml, R"(gh release edit[^\n]*--draft=false)"),
        "release.yml must publish the draft explicitly once its assets are verified");

    const size_t createAt = findCommandLine(yaml, "gh release create");
    const size_t undraftAt = findCommandLine(yaml, "gh release edit");
    TEST_ASSERT_TRUE_MESSAGE(createAt != std::string::npos && undraftAt != std::string::npos,
                             "release.yml must both create and un-draft the release");
    TEST_ASSERT_TRUE_MESSAGE(createAt < undraftAt,
                             "the release must be created as a draft before it is un-drafted");
}

// Bad weather: exactly 10 assets ship (3 firmware + 3 littlefs + 3 flashparts
// zips + manifest.json, see build-scripts/make_manifest.py and
// test_release_reuses_test_jobs_build_artifacts_instead_of_rebuilding below).
// Asserting the count in the workflow means an asset silently added or
// dropped fails the release rather than publishing a set the devices can't
// use - see test_expected_asset_count_is_derived_not_a_literal, which pins
// that the count itself is computed, not hard-coded, after this drifted once
// (7 -> 10) when the flashparts zips landed (#181).
void test_release_verifies_the_asset_count_before_publishing()
{
    std::string yaml = stripComments(readFile(".github/workflows/release.yml"));

    TEST_ASSERT_TRUE_MESSAGE(
        yaml.find("EXPECTED_ASSETS") != std::string::npos,
        "release.yml must assert the expected asset count (EXPECTED_ASSETS) before un-drafting");

    const size_t checkAt = yaml.find("EXPECTED_ASSETS");
    const size_t undraftAt = findCommandLine(yaml, "gh release edit");
    TEST_ASSERT_TRUE_MESSAGE(checkAt < undraftAt,
                             "the asset-count check must run before the release is made visible");
}

// Bad weather, and a hazard the two-step publish introduced: a run can now
// fail *after* the draft exists, and `gh release create` refuses a tag that
// already has a release - so one failed attempt would block every retry of
// that tag. The draft must be cleared first, and only ever a draft: deleting a
// published release would destroy one the fleet may already have seen.
void test_release_clears_a_leftover_draft_before_recreating()
{
    std::string yaml = stripComments(readFile(".github/workflows/release.yml"));

    TEST_ASSERT_TRUE_MESSAGE(
        yaml.find("gh release delete") != std::string::npos,
        "release.yml must clear a leftover draft before creating, or a single failed run "
        "permanently blocks retrying that tag");
    TEST_ASSERT_TRUE_MESSAGE(
        yaml.find("isDraft") != std::string::npos,
        "the delete must be guarded on isDraft - a published release must never be deleted");

    const size_t deleteAt = findCommandLine(yaml, "gh release delete");
    const size_t createAt = findCommandLine(yaml, "gh release create");
    TEST_ASSERT_TRUE_MESSAGE(deleteAt < createAt,
                             "the leftover draft must be cleared before the release is created");
}

// Bad weather: a device cannot fetch a draft, so a draft sets no version bar.
// Counting one - and release.yml can leave one behind on a failed run - would
// block the retry of the very release that failed, blaming the wrong thing.
void test_version_code_gate_ignores_drafts()
{
    std::string script = readFile("build-scripts/check_version_code.py");

    TEST_ASSERT_TRUE_MESSAGE(
        script.find("isDraft") != std::string::npos,
        "check_version_code.py must ask for isDraft and skip drafts when computing the "
        "highest published version code");
}

// --- R4: the #107 accept guard must not pass by finding nothing -----------

// Good weather: the guard stays wired as a post-action on the hardware builds.
// (test_async_library_pin asserts the platformio.ini side; this is the script
// side.)
void test_accept_guard_script_still_registers_a_post_action()
{
    std::string script = readFile("build-scripts/check_asynctcp_accept_guard.py");
    TEST_ASSERT_TRUE_MESSAGE(script.find("AddPostAction") != std::string::npos,
                             "check_asynctcp_accept_guard.py must stay a post-build action");
}

// Bad weather: the guard used to `return` (pass, exit 0, print nothing) when
// its glob matched no files. A lib_deps rename, an upstream library.json name
// change, or a half-populated libdeps dir would then switch off the #107 crash
// gate with the whole suite green - including on the release build. It must
// decide whether AsyncTCP is expected from lib_deps, not from whether the glob
// happened to hit.
void test_accept_guard_fails_when_it_finds_nothing_to_check()
{
    std::string script = readFile("build-scripts/check_asynctcp_accept_guard.py");

    TEST_ASSERT_TRUE_MESSAGE(
        script.find("GetProjectOption") != std::string::npos,
        "check_asynctcp_accept_guard.py must read lib_deps (env.GetProjectOption) to decide "
        "whether this env is expected to have AsyncTCP, instead of silently passing when its "
        "glob matches nothing");

    TEST_ASSERT_TRUE_MESSAGE(
        script.find("ACCEPT_GUARD_CHECK_SKIPPED") != std::string::npos,
        "check_asynctcp_accept_guard.py must print a greppable marker when it skips, so a "
        "silently-disabled guard is visible in the build log");

    // The specific regression: an unconditional bare `return` on the
    // empty-candidates path. Any exit from that branch must now be a failure
    // (sys.exit) or an explicit, logged skip.
    TEST_ASSERT_FALSE_MESSAGE(
        contains(script, R"(if not candidates:\s*\n(?:\s*#[^\n]*\n)*\s*return\s*\n)"),
        "check_asynctcp_accept_guard.py must not pass silently when no AsyncTCP.cpp is found");
}

// --- R5: CI must build the filesystem image -------------------------------

// Bad weather: hardware-build only ran `pio run`, so the LittleFS image was
// first built at release time - after the firmware halves were already staged.
// A data/ change that broke or oversized it passed every PR check.
void test_ci_builds_the_filesystem_image()
{
    std::string yaml = stripComments(readFile(".github/workflows/test.yml"));

    TEST_ASSERT_TRUE_MESSAGE(
        contains(yaml, R"(-t\s+buildfs)"),
        "test.yml's hardware-build must run `-t buildfs` so a broken or oversized data/ image "
        "fails on the PR, not on the release tag");
}

// Good weather: the spiffs partition the image has to fit in is unchanged, and
// the size check reads its ceiling from the partition table rather than
// hard-coding a number that would drift from it.
void test_fs_image_ceiling_comes_from_the_partition_table()
{
    std::string csv = readFile("partitions/andromeda_4mb.csv");
    TEST_ASSERT_TRUE_MESSAGE(contains(csv, R"(spiffs[^\n]*0xA0000)"),
                             "the spiffs partition must still be 0xA0000 (640 KiB)");

    std::string script = readFile("build-scripts/check_fs_ceiling.py");
    TEST_ASSERT_TRUE_MESSAGE(
        script.find("$PARTITIONS_TABLE_CSV") != std::string::npos,
        "check_fs_ceiling.py must size the image against the partition table PlatformIO "
        "resolved for the env ($PARTITIONS_TABLE_CSV), not a hard-coded byte count or "
        "filename that could drift from board_build.partitions");
    TEST_ASSERT_TRUE_MESSAGE(script.find(_DATA_SUBTYPE_LITERAL) != std::string::npos,
                             "check_fs_ceiling.py must size the image against the spiffs row");
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_test_workflow_keeps_its_existing_triggers);
    RUN_TEST(test_release_job_is_gated_on_the_test_suite);
    RUN_TEST(test_release_checks_version_code_is_newer_than_the_published_one);
    RUN_TEST(test_release_is_published_as_a_verified_draft);
    RUN_TEST(test_release_verifies_the_asset_count_before_publishing);
    RUN_TEST(test_release_clears_a_leftover_draft_before_recreating);
    RUN_TEST(test_version_code_gate_ignores_drafts);
    RUN_TEST(test_accept_guard_script_still_registers_a_post_action);
    RUN_TEST(test_accept_guard_fails_when_it_finds_nothing_to_check);
    RUN_TEST(test_ci_builds_the_filesystem_image);
    RUN_TEST(test_fs_image_ceiling_comes_from_the_partition_table);
    return UNITY_END();
}
