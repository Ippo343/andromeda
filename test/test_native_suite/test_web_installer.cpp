// Static-analysis test, not a build/runtime one: reads the web-installer
// pipeline's own files as plain text (build-scripts/dump_flash_parts.py,
// build-scripts/assemble_site.py, the workflows, web-installer/) and asserts
// the invariants #162's design depends on are still wired. Same approach as
// test_release_pipeline and test_static_assets - no libdeps, no toolchain,
// no network, no PlatformIO/browser runtime needed.
//
// What's guarded, and why each one matters:
//
//   - dump_flash_parts.py's offsets come only from PlatformIO's resolved
//     build env, never a literal - the bootloader address alone differs by
//     chip (0x1000 on ESP32, 0x0 on S3/C3), so a hard-coded offset would be
//     wrong on two of the three boards.
//   - assemble_site.py's chipFamily spelling and new_install_prompt_erase
//     are ESP Web Tools contract: a typo in the former just means "chip not
//     supported" with no error anywhere, and true on the latter offers a
//     full chip erase - destroying the NVS that multi-part flashing exists
//     to protect.
//   - the NVS-overlap validation exists at all (test_assemble_site_logic.py
//     style unit coverage isn't practical here without a Python test
//     runner in the native suite, so this pins the source instead).
//   - the shared-styling copy (data/css/common.css et al): the on-device
//     Cinzel font subset is stripped out (not shipped, not rewritten - see
//     _copy_shared_ui_assets()'s docstring), the one thing that would
//     otherwise 404 under a project-page subpath with no CORS-friendly fix.
//   - the CI wiring itself (test.yml's hardware-build + web-installer-assemble
//     jobs) - the "exercised on every commit" contract, so a future edit
//     can't quietly drop this pipeline from CI.
//   - GITHUB_TOKEN-raised events don't trigger workflows, so pages.yml must
//     be `uses:`-chained from release.yml, not `on: release` alone.
//   - the CORS constraint that shapes the whole design: no code path may try
//     to fetch a releases/download URL from the browser.
//   - the release the installer serves is chosen by version code (not publish
//     date) and its bytes are md5-checked against the manifest before
//     assembly; the assembled manifest and the live deployed site are both
//     verified - a green run can't mean an unchecked or unreachable installer.
//   - installer-logic.js (shipped + unit-tested) is actually called by
//     installer.js - a tested module the page never runs is worse than none.
#include <unity.h>

#include <fstream>
#include <regex>
#include <sstream>
#include <string>

void test_web_installer_setUp() {}
void test_web_installer_tearDown() {}

namespace
{
static std::string readFile(const std::string& path)
{
    std::ifstream file(path);
    TEST_ASSERT_TRUE_MESSAGE(file.is_open(), ("Could not open " + path).c_str());
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

// A workflow with every '#'-comment line dropped - see test_release_pipeline
// for why (workflows narrate their own history in comments).
static std::string stripComments(const std::string& yaml)
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

static bool contains(const std::string& haystack, const std::string& re)
{
    return std::regex_search(haystack, std::regex(re));
}

static size_t findCommandLine(const std::string& yaml, const std::string& command)
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
}  // namespace

// --- dump_flash_parts.py: offsets from PlatformIO, never literals ---------

void test_dump_flash_parts_reads_offsets_from_platformio()
{
    std::string script = readFile("build-scripts/dump_flash_parts.py");

    TEST_ASSERT_TRUE_MESSAGE(script.find("FLASH_EXTRA_IMAGES") != std::string::npos,
                             "dump_flash_parts.py must read the bootloader/partitions/boot_app0 "
                             "offsets from PlatformIO's FLASH_EXTRA_IMAGES");
    TEST_ASSERT_TRUE_MESSAGE(script.find("$ESP32_APP_OFFSET") != std::string::npos,
                             "dump_flash_parts.py must read the app offset from $ESP32_APP_OFFSET");
    TEST_ASSERT_TRUE_MESSAGE(script.find("$PARTITIONS_TABLE_CSV") != std::string::npos,
                             "dump_flash_parts.py must read the filesystem/nvs offsets from the "
                             "resolved $PARTITIONS_TABLE_CSV, not a literal");
}

// Bad weather: a hard-coded offset would be wrong on two of the three boards
// (bootloader sits at 0x1000 on ESP32 but 0x0 on S3/C3). Strip the one block
// that is legitimately allowed to mention flash-size *codes* (0/1/2/3/4, not
// byte offsets), then assert none of the real offsets appear as literals.
void test_dump_flash_parts_has_no_offset_literals()
{
    std::string script = readFile("build-scripts/dump_flash_parts.py");
    std::string withoutComments = stripComments(script);

    TEST_ASSERT_FALSE_MESSAGE(
        contains(withoutComments, R"(0x(1000|8000|e000|10000|350000)\b)"),
        "dump_flash_parts.py must not hard-code any boot/app/fs offset - they must come from "
        "FLASH_EXTRA_IMAGES / $ESP32_APP_OFFSET / $PARTITIONS_TABLE_CSV instead");
}

void test_partition_table_still_has_nvs_and_spiffs_rows()
{
    std::string csv = readFile("partitions/andromeda_4mb.csv");
    TEST_ASSERT_TRUE_MESSAGE(contains(csv, R"(nvs[^\n]*0x9000)"),
                             "dump_flash_parts.py's 'preserve' list reads the nvs row's offset");
    TEST_ASSERT_TRUE_MESSAGE(contains(csv, R"(spiffs[^\n]*0x350000)"),
                             "dump_flash_parts.py's littlefs offset reads the spiffs row");
}

// --- assemble_site.py: the ESP Web Tools manifest contract -----------------

void test_assemble_site_chip_family_spelling_is_exact()
{
    std::string script = readFile("build-scripts/assemble_site.py");
    // The mapping the manifest's chipFamily values ultimately come from lives
    // in dump_flash_parts.py; pin it there, where the literal strings are.
    std::string dumpScript = readFile("build-scripts/dump_flash_parts.py");

    TEST_ASSERT_TRUE_MESSAGE(dumpScript.find("\"ESP32\"") != std::string::npos &&
                                 dumpScript.find("\"ESP32-S3\"") != std::string::npos &&
                                 dumpScript.find("\"ESP32-C3\"") != std::string::npos,
                             "chipFamily values must be spelled exactly ESP32 / ESP32-S3 / "
                             "ESP32-C3 - a typo doesn't error, it just means ESP Web Tools "
                             "reports the chip as unsupported");
    TEST_ASSERT_TRUE_MESSAGE(script.find("chipFamily") != std::string::npos,
                             "assemble_site.py must carry chipFamily through into manifest.json");
}

// Bad weather: true offers a full chip erase, destroying the NVS that
// multi-part flashing (parts at their real offsets, never the whole chip)
// exists to protect - see the NVS-overlap check below.
void test_assemble_site_never_prompts_a_full_erase()
{
    std::string script = readFile("build-scripts/assemble_site.py");

    TEST_ASSERT_TRUE_MESSAGE(script.find("new_install_prompt_erase") != std::string::npos,
                             "assemble_site.py must set new_install_prompt_erase explicitly");
    TEST_ASSERT_TRUE_MESSAGE(
        contains(script, R"("new_install_prompt_erase":\s*False)"),
        "new_install_prompt_erase must be False - True offers a full chip erase and destroys NVS");
}

void test_assemble_site_validates_no_nvs_overlap()
{
    std::string script = readFile("build-scripts/assemble_site.py");

    TEST_ASSERT_TRUE_MESSAGE(script.find("preserve") != std::string::npos,
                             "assemble_site.py must validate every part against the 'preserve' "
                             "list dump_flash_parts.py emits for the nvs range");
    TEST_ASSERT_TRUE_MESSAGE(
        script.find("overlaps preserved") != std::string::npos,
        "assemble_site.py must refuse (hard exit) to publish a manifest where a part's byte "
        "range overlaps a preserved range, rather than silently allowing it");
}

// --- board list consistency -------------------------------------------------

void test_assemble_site_boards_list_matches_ota_manifest_and_board_variant()
{
    std::string assembleScript = readFile("build-scripts/assemble_site.py");
    std::string otaManifestScript = readFile("build-scripts/make_manifest.py");
    std::string boardVariant = readFile("include/board-variant.h");

    for (const std::string& board : {"esp32_wroom", "esp32_s3_zero", "esp32_c3_zero"})
    {
        TEST_ASSERT_TRUE_MESSAGE(assembleScript.find(board) != std::string::npos,
                                 ("assemble_site.py's BOARDS list is missing " + board).c_str());
        TEST_ASSERT_TRUE_MESSAGE(otaManifestScript.find(board) != std::string::npos,
                                 ("make_manifest.py's BOARDS list is missing " + board).c_str());
        TEST_ASSERT_TRUE_MESSAGE(boardVariant.find(board) != std::string::npos,
                                 ("include/board-variant.h is missing " + board).c_str());
    }
}

// --- shared on-device styling ----------------------------------------------

void test_assemble_site_reuses_the_on_device_ui_assets()
{
    std::string script = readFile("build-scripts/assemble_site.py");

    for (const std::string& asset : {"common.css", "utils.js"})
    {
        TEST_ASSERT_TRUE_MESSAGE(script.find(asset) != std::string::npos,
                                 ("assemble_site.py must copy the on-device " + asset +
                                  " into the site, so the "
                                  "installer looks like the real web UI")
                                     .c_str());
    }
}

// The on-device font is a Cinzel Decorative subset containing only the glyph
// "A", to keep the flashed filesystem small - a constraint that doesn't
// apply to a webpage. assemble_site.py must strip that local @font-face
// rather than ship the subset (which would be pointlessly reused for no
// footprint benefit and would need its own /fonts/ URL rewrite to boot); the
// page must instead load the real face from Google Fonts.
void test_assemble_site_does_not_ship_the_on_device_font_subset()
{
    std::string script = readFile("build-scripts/assemble_site.py");
    std::string html = readFile("web-installer/index.html");

    TEST_ASSERT_TRUE_MESSAGE(
        script.find("font-face") != std::string::npos,
        "assemble_site.py must strip the local Cinzel @font-face out of the copied common.css");
    TEST_ASSERT_FALSE_MESSAGE(
        contains(script, R"(copyfile\([^\n]*cinzel)"),
        "assemble_site.py must not copy the on-device cinzel.woff2 subset into the site");

    TEST_ASSERT_TRUE_MESSAGE(
        contains(html, R"(fonts\.googleapis\.com.*Cinzel\+Decorative)"),
        "index.html must load the real Cinzel Decorative face from Google Fonts");
}

// Bad weather, caught by actually flashing a board from the live site (#162):
// assemble_site.py copies utils.js into the site, but copying the file isn't
// enough - .logo's background-clip: text renders invisible unless something
// sets --grad, same as data/js/controls.js's initControlsPage() does. Without
// both the <script> tag AND the call, the page loads with a blank logo and no
// error anywhere.
void test_installer_page_sets_the_logo_gradient()
{
    std::string html = readFile("web-installer/index.html");
    std::string installerJs = readFile("web-installer/js/installer.js");

    TEST_ASSERT_TRUE_MESSAGE(html.find("js/utils.js") != std::string::npos,
                             "index.html must load js/utils.js - it defines randomGradient()");
    TEST_ASSERT_TRUE_MESSAGE(
        installerJs.find("randomGradient()") != std::string::npos,
        "installer.js must call randomGradient() and set it as --grad on #logo, or the logo "
        "renders invisible (background-clip: text with nothing to clip)");
}

// Bad weather (#192): installer-logic.js ships to the browser and is
// unit-tested (test/js/installer-logic.test.js), but for a long time
// installer.js never called any of it - the tested logic wasn't the shipped
// logic, and the parts table version.json carries had nowhere to render.
// A module in that state is worse than none: the tests read as coverage the
// page doesn't actually have. Assert installer.js calls into it.
void test_installer_js_actually_calls_installer_logic()
{
    std::string installerJs = readFile("web-installer/js/installer.js");
    std::string installerLogicJs = readFile("web-installer/js/installer-logic.js");

    TEST_ASSERT_TRUE_MESSAGE(
        installerLogicJs.find("function formatPartsTable") != std::string::npos,
        "installer-logic.js must still export formatPartsTable - installer.js and its "
        "test both depend on it");
    TEST_ASSERT_TRUE_MESSAGE(
        installerJs.find("formatPartsTable(") != std::string::npos,
        "installer.js must call formatPartsTable() from installer-logic.js - a shipped, "
        "unit-tested module the page never executes is dead code masquerading as coverage");
    TEST_ASSERT_TRUE_MESSAGE(
        readFile("web-installer/index.html").find("id=\"parts\"") != std::string::npos,
        "index.html must have the #parts container installer.js renders the flash layout into");
}

// Good weather: the source files this rewrite depends on still look the way
// the rewrite assumes - if either changes shape, the rewrite silently
// becomes a no-op (a wrong assumption fails loudly here, not on the live site).
void test_shared_ui_assets_still_match_what_assemble_site_expects()
{
    std::string commonCss = readFile("data/css/common.css");
    TEST_ASSERT_TRUE_MESSAGE(
        contains(commonCss, R"(@font-face\s*\{\s*font-family:\s*'Cinzel Decorative')"),
        "data/css/common.css no longer has the local Cinzel @font-face block "
        "assemble_site.py's strip expects");

    std::string utilsJs = readFile("data/js/utils.js");
    TEST_ASSERT_TRUE_MESSAGE(utilsJs.find("randomGradient") != std::string::npos,
                             "data/js/utils.js no longer defines randomGradient()");
}

// --- pages.yml ---------------------------------------------------------

void test_pages_workflow_is_a_reusable_artifact_deploy()
{
    std::string yaml = stripComments(readFile(".github/workflows/pages.yml"));

    TEST_ASSERT_TRUE_MESSAGE(yaml.find("workflow_call:") != std::string::npos,
                             "pages.yml must be callable via workflow_call - release.yml chains "
                             "it directly, since GITHUB_TOKEN-raised events never trigger runs");
    TEST_ASSERT_TRUE_MESSAGE(contains(yaml, R"(pages:\s*write)"),
                             "pages.yml needs pages: write to deploy");
    TEST_ASSERT_TRUE_MESSAGE(contains(yaml, R"(id-token:\s*write)"),
                             "pages.yml needs id-token: write to deploy");
    TEST_ASSERT_TRUE_MESSAGE(yaml.find("upload-pages-artifact") != std::string::npos &&
                                 yaml.find("deploy-pages") != std::string::npos,
                             "pages.yml must deploy via the official Pages actions");

    TEST_ASSERT_FALSE_MESSAGE(yaml.find("gh-pages") != std::string::npos,
                              "no gh-pages branch - binaries must never be committed to git");
    TEST_ASSERT_FALSE_MESSAGE(yaml.find("peaceiris") != std::string::npos,
                              "no third-party Pages action - the official artifact-deploy actions "
                              "are sufficient and need no extra trust");
    TEST_ASSERT_FALSE_MESSAGE(contains(yaml, R"(git\s+push)"),
                              "the deploy must never push to a branch");
}

// #193: pages.yml downloads firmware-*/littlefs-* release assets and serves
// them as the exact bytes a stranger flashes onto a bare board, but never
// checked them against anything. make_manifest.py ships an md5 for exactly
// those bytes in manifest.json.
void test_pages_workflow_verifies_downloaded_binaries_against_manifest_md5()
{
    std::string yaml = stripComments(readFile(".github/workflows/pages.yml"));

    // Good weather: manifest.json is pulled alongside the binaries and its md5
    // is what the check compares against.
    TEST_ASSERT_TRUE_MESSAGE(
        contains(yaml, R"(--pattern 'manifest\.json')"),
        "pages.yml must download manifest.json with the release binaries so it can verify them");
    TEST_ASSERT_TRUE_MESSAGE(
        yaml.find("md5sum") != std::string::npos && yaml.find(".md5") != std::string::npos,
        "pages.yml must md5sum each downloaded firmware/littlefs binary against the manifest's "
        "md5");

    // Bad weather: verifying *after* assemble_site.py has already built _site
    // from the bytes would be pointless - the check must gate the assembly.
    const size_t verifyAt = yaml.find("md5sum");
    const size_t assembleAt = yaml.find("assemble_site.py");
    TEST_ASSERT_TRUE_MESSAGE(
        verifyAt != std::string::npos && assembleAt != std::string::npos && verifyAt < assembleAt,
        "the md5 verification must run before assemble_site.py, not after");
}

// #195: "newest stable" was `sort_by(.publishedAt) | last` - re-publishing an
// older release, or un-drafting a hotfix cut from an older tag, would serve
// that older firmware to every brand-new device. check_version_code.py guards
// this for OTA; the web installer had no equivalent, and here a wrong choice
// directly determines what a bare board gets flashed with.
void test_pages_workflow_resolves_newest_release_by_version_not_date()
{
    std::string yaml = stripComments(readFile(".github/workflows/pages.yml"));

    // Good weather: the resolve step consults a version code / manifest, not
    // only publishedAt.
    size_t resolveAt = yaml.find("Resolve the newest stable release");
    size_t downloadAt = yaml.find("Download the released assets");
    TEST_ASSERT_TRUE_MESSAGE(
        resolveAt != std::string::npos && downloadAt != std::string::npos && resolveAt < downloadAt,
        "expected the Resolve step to still precede the Download step");
    std::string resolveStep = yaml.substr(resolveAt, downloadAt - resolveAt);
    TEST_ASSERT_TRUE_MESSAGE(
        resolveStep.find("versionCode") != std::string::npos &&
            resolveStep.find("manifest.json") != std::string::npos,
        "the resolve step must pick by the manifest's versionCode, like check_version_code.py, "
        "not by publish date alone");

    // Bad weather: `sort_by(.publishedAt) | last` must not be the whole
    // selector any more (a plain `| reverse` for tie-breaking is fine).
    TEST_ASSERT_FALSE_MESSAGE(
        contains(resolveStep, R"(sort_by\(\.publishedAt\)\s*\|\s*last\b)"),
        "`sort_by(.publishedAt) | last` alone must no longer choose the release the installer "
        "serves");
}

// #198: deploy-pages reporting success only means a deployment was created,
// not that the site answers. A truncated artifact / propagation failure / a
// wrong output subdirectory would end the run green with an installer that
// 404s. pages.yml must fetch the live URL and fail if it doesn't serve what
// it published.
void test_pages_workflow_verifies_the_deployed_site_answers()
{
    std::string yaml = stripComments(readFile(".github/workflows/pages.yml"));

    // Good weather: a step consumes the deployment's live page_url.
    size_t verifyAt = yaml.find("Verify the deployed site answers");
    TEST_ASSERT_TRUE_MESSAGE(verifyAt != std::string::npos,
                             "pages.yml must have a post-deploy step that checks the live site");
    std::string verifyStep = yaml.substr(verifyAt);
    TEST_ASSERT_TRUE_MESSAGE(
        verifyStep.find("steps.deployment.outputs.page_url") != std::string::npos,
        "the post-deploy check must fetch steps.deployment.outputs.page_url, not a guessed URL");

    // Bad weather: it must run *after* deploy-pages, and be able to fail the
    // job (a curl -f against the live URL), so green can't mean unreachable.
    const size_t deployAt = yaml.find("actions/deploy-pages");
    TEST_ASSERT_TRUE_MESSAGE(deployAt != std::string::npos && deployAt < verifyAt,
                             "the live-site check must come after actions/deploy-pages");
    TEST_ASSERT_TRUE_MESSAGE(
        contains(verifyStep, R"(curl\s+-f)"),
        "the live-site check must use curl -f (or equivalent) so a 404/unreachable site exits "
        "non-zero instead of passing silently");
}

void test_release_workflow_chains_pages_on_stable_only()
{
    std::string yaml = stripComments(readFile(".github/workflows/release.yml"));

    TEST_ASSERT_TRUE_MESSAGE(
        yaml.find("./.github/workflows/pages.yml") != std::string::npos,
        "release.yml must chain a call to pages.yml - an `on: release` trigger there alone "
        "would never fire, since gh release edit runs as GITHUB_TOKEN");
    TEST_ASSERT_TRUE_MESSAGE(
        contains(yaml, R"(prerelease\s*==\s*'false')"),
        "the Pages deploy must be gated to a stable release - the web installer never serves "
        "a dev-channel pre-release");
}

// --- release.yml: flashparts staged, ordered after buildfs -----------------

// The actual build (buildfs then flashparts, in order) now lives in
// test.yml's hardware-build job - see test_ci_builds_flashparts_and_
// assembles_the_site_on_every_push below for that ordering pin. release.yml
// no longer builds anything (#162: reuses those images instead of
// rebuilding); this only pins that it stages what it downloaded correctly.
void test_hardware_build_stages_flashparts_after_buildfs()
{
    std::string yaml = stripComments(readFile(".github/workflows/test.yml"));

    // Plain substring search, not findCommandLine: the buildfs step is the
    // single-line `run: pio run ...` form, so "pio run" isn't the first
    // token on its line the way findCommandLine expects.
    const size_t buildfsAt = yaml.find("pio run -e ${{ matrix.env }} -t buildfs");
    const size_t flashpartsAt = yaml.find("pio run -e ${{ matrix.env }} -t flashparts");
    TEST_ASSERT_TRUE_MESSAGE(
        buildfsAt != std::string::npos && flashpartsAt != std::string::npos,
        "test.yml's hardware-build must run both -t buildfs and -t flashparts");
    TEST_ASSERT_TRUE_MESSAGE(buildfsAt < flashpartsAt,
                             "-t flashparts requires littlefs.bin to already exist, so it must "
                             "run after -t buildfs");
}

// Bad weather: rebuilding the images a second time in the release job wastes
// several minutes and risks a second, non-identical build silently diverging
// from the one test.yml's hardware-build already produced and gated -
// release.yml must download and reuse those artifacts instead.
void test_release_reuses_test_jobs_build_artifacts_instead_of_rebuilding()
{
    std::string yaml = stripComments(readFile(".github/workflows/release.yml"));

    TEST_ASSERT_FALSE_MESSAGE(
        contains(yaml, R"(pio run)"),
        "release.yml must not invoke `pio run` at all - every image it ships must come from "
        "test.yml's hardware-build job via download-artifact, not a second build");
    TEST_ASSERT_TRUE_MESSAGE(
        contains(yaml, R"(pattern:\s*"flashparts-\*")"),
        "release.yml must download the flashparts-<env> artifacts test.yml's hardware-build "
        "job uploaded");

    TEST_ASSERT_TRUE_MESSAGE(
        yaml.find("flashparts-$env-$TAG.zip") != std::string::npos,
        "release.yml must stage each board's boot parts as a flashparts-<env>-<tag>.zip asset");

    const size_t zipAt = yaml.find("flashparts-$env-$TAG.zip");
    const size_t createAt = findCommandLine(yaml, "gh release create");
    TEST_ASSERT_TRUE_MESSAGE(
        zipAt != std::string::npos && createAt != std::string::npos && zipAt < createAt,
        "the flashparts zips must be staged before the release is created");
}

// #162: reusing test.yml's build artifacts is only correct if the version
// baked into the binary by inject_version.py (git rev-list --count HEAD /
// git describe) is the real one - which needs full git history, not the
// default shallow checkout.
void test_hardware_build_checkout_has_full_history()
{
    std::string yaml = stripComments(readFile(".github/workflows/test.yml"));

    const size_t jobAt = yaml.find("hardware-build:");
    const size_t nextJobAt = yaml.find("web-installer-assemble:");
    TEST_ASSERT_TRUE_MESSAGE(
        jobAt != std::string::npos && nextJobAt != std::string::npos && jobAt < nextJobAt,
        "expected job layout in test.yml has changed - update this test");
    std::string hardwareBuildJob = yaml.substr(jobAt, nextJobAt - jobAt);

    TEST_ASSERT_TRUE_MESSAGE(
        contains(hardwareBuildJob, R"(fetch-depth:\s*0)"),
        "test.yml's hardware-build checkout must use fetch-depth: 0 - on the default shallow "
        "checkout, inject_version.py's git rev-list --count / git describe silently produce a "
        "wrong version baked into the binary, invisible until release.yml reuses that exact "
        "image instead of rebuilding");
}

// #181: the expected asset count must be derived from what was actually
// staged, not a literal - a literal is exactly what drifted here (7 -> 10)
// when the flashparts zips landed.
void test_expected_asset_count_is_derived_not_a_literal()
{
    std::string yaml = stripComments(readFile(".github/workflows/release.yml"));

    TEST_ASSERT_TRUE_MESSAGE(
        contains(yaml, R"(EXPECTED_ASSETS=\$\()"),
        "EXPECTED_ASSETS must be computed from dist/ (e.g. `find dist ... | wc -l`), not "
        "assigned a literal number that can silently drift from what's actually staged");
    TEST_ASSERT_FALSE_MESSAGE(
        contains(yaml, R"(EXPECTED_ASSETS=7\b)"),
        "the old hard-coded count of 7 must be gone - the set grew to 10 with the flashparts zips");
}

// --- CORS constraint that shapes the whole design ---------------------------

// Bad weather: GitHub release-asset downloads send no
// Access-Control-Allow-Origin header (verified against a real release asset
// - api.github.com sends one, release downloads don't), so any client-side
// fetch of one from the Pages origin would fail silently in the browser. The
// binaries must always be served same-origin instead.
void test_no_code_path_fetches_release_downloads_from_the_browser()
{
    std::string html = readFile("web-installer/index.html");
    std::string installerJs = readFile("web-installer/js/installer.js");
    std::string installerLogicJs = readFile("web-installer/js/installer-logic.js");
    std::string assembleScript = readFile("build-scripts/assemble_site.py");

    TEST_ASSERT_FALSE_MESSAGE(
        html.find("releases/download") != std::string::npos,
        "web-installer/index.html must not reference a releases/download URL");
    TEST_ASSERT_FALSE_MESSAGE(
        installerJs.find("releases/download") != std::string::npos,
        "web-installer/js/installer.js must not fetch a releases/download URL");
    TEST_ASSERT_FALSE_MESSAGE(
        installerLogicJs.find("releases/download") != std::string::npos,
        "web-installer/js/installer-logic.js must not reference a releases/download URL");
    TEST_ASSERT_FALSE_MESSAGE(
        assembleScript.find("releases/download") != std::string::npos,
        "assemble_site.py must not embed a releases/download URL into the site it assembles");
}

// --- esp-web-tools pin -------------------------------------------------------

void test_esp_web_tools_is_pinned_to_an_exact_version()
{
    std::string html = readFile("web-installer/index.html");

    TEST_ASSERT_TRUE_MESSAGE(html.find("esp-web-tools@") != std::string::npos,
                             "index.html must load esp-web-tools");
    TEST_ASSERT_TRUE_MESSAGE(
        contains(html, R"(esp-web-tools@\d+\.\d+\.\d+)"),
        "esp-web-tools must be pinned to an exact M.m.p version - an upstream release could "
        "otherwise change how strangers flash their boards with no commit in this repo");
    TEST_ASSERT_FALSE_MESSAGE(html.find("esp-web-tools@latest") != std::string::npos,
                              "esp-web-tools must never be loaded via @latest");
}

// --- Security: the resolved release tag must never be interpolated -------
// --- straight into a shell script or into innerHTML -----------------------

// Git tag names aren't restricted from containing shell metacharacters -
// quotes, `$()`, backticks are all legal in a ref name - so a literal
// `${{ steps.rel.outputs.tag }}` inside a `run:` block would let anyone able
// to push a tag inject commands into this job, which holds
// pages:write/id-token:write. The fix is to pass it through `env:` and refer
// to it as a shell variable instead, so GitHub substitutes it as data, not
// as script text.
void test_pages_workflow_never_interpolates_the_tag_directly_into_a_script()
{
    std::string yaml = stripComments(readFile(".github/workflows/pages.yml"));

    // The vulnerable shape: the expression sitting inside quotes as if it
    // were a literal shell token (`TAG='${{ ... }}'`, `--tag '${{ ... }}'`).
    // Passing it through `env:` (asserted below) is the only place the raw
    // `${{ steps.rel.outputs.tag }}` token may still appear - GitHub
    // substitutes env: values as data, never as script text, so that spot is
    // safe by construction.
    TEST_ASSERT_FALSE_MESSAGE(
        contains(yaml, R"(['"]\$\{\{\s*steps\.rel\.outputs\.tag\s*\}\}['"])"),
        "pages.yml must not interpolate steps.rel.outputs.tag directly into a run: script - a "
        "git tag name can contain shell metacharacters (quotes, $(), backticks are all legal in "
        "a ref name); pass it through env: and reference it as a shell variable instead");
    TEST_ASSERT_TRUE_MESSAGE(
        contains(yaml, R"(REL_TAG:\s*\$\{\{\s*steps\.rel\.outputs\.tag\s*\}\})"),
        "pages.yml must assign steps.rel.outputs.tag to an env var (REL_TAG) rather than "
        "interpolating it inline");
    TEST_ASSERT_TRUE_MESSAGE(yaml.find("\"$REL_TAG\"") != std::string::npos ||
                                 yaml.find("$REL_TAG") != std::string::npos,
                             "pages.yml must reference the tag as the $REL_TAG shell variable, "
                             "not re-interpolate the raw expression");
}

// versionInfo.tag/releaseUrl (data/version.json, written by assemble_site.py
// from the same git tag) must never be assigned through innerHTML: a tag
// isn't restricted from containing HTML either, and this page is public.
// installer.js must build the version line with DOM APIs (textContent /
// createElement) instead, which never parse their input as markup.
void test_installer_js_never_assigns_version_info_via_innerhtml()
{
    std::string script = readFile("web-installer/js/installer.js");

    TEST_ASSERT_FALSE_MESSAGE(
        contains(script, R"(innerHTML\s*=[^=])"),
        "installer.js must not use innerHTML anywhere - versionInfo.tag/releaseUrl trace back "
        "to the pushed git tag name, which isn't restricted from containing HTML; a malicious "
        "or accidental tag landing in a real release must not execute as script on this public "
        "page. Use textContent/createElement instead.");
    TEST_ASSERT_TRUE_MESSAGE(script.find("versionEl.textContent = ") != std::string::npos,
                             "installer.js must build the version line via safe DOM APIs "
                             "(textContent/createElement), not string-concatenated markup");
}

// --- CI wiring: exercised on every commit, not a manual dry-run ------------

void test_ci_builds_flashparts_and_assembles_the_site_on_every_push()
{
    std::string yaml = stripComments(readFile(".github/workflows/test.yml"));

    TEST_ASSERT_TRUE_MESSAGE(
        contains(yaml, R"(-t\s+flashparts)"),
        "test.yml's hardware-build must run -t flashparts, so a broken offset or manifest fails "
        "on the PR rather than only ever being exercised at release time");
    TEST_ASSERT_TRUE_MESSAGE(
        yaml.find("assemble_site.py") != std::string::npos,
        "test.yml must call build-scripts/assemble_site.py directly, so the exact validation "
        "pages.yml relies on for a real deploy runs on every push/PR too");
}

// #196: assemble_site.py validates its inputs but nothing inspected the
// manifest.json / version.json it wrote. check_site.py does, and must run in
// both the CI assemble job and the real deploy - right after assemble_site.py,
// so a green assembly can no longer mean an unchecked site.
void test_both_workflows_check_the_assembled_site()
{
    for (const char* wf : {".github/workflows/test.yml", ".github/workflows/pages.yml"})
    {
        std::string yaml = stripComments(readFile(wf));
        const size_t assembleAt = yaml.find("assemble_site.py");
        const size_t checkAt = yaml.find("check_site.py");
        TEST_ASSERT_TRUE_MESSAGE(
            assembleAt != std::string::npos && checkAt != std::string::npos && assembleAt < checkAt,
            (std::string(wf) + " must run build-scripts/check_site.py after assemble_site.py")
                .c_str());
    }
}

void run_test_web_installer_tests()
{
    RUN_TEST(test_dump_flash_parts_reads_offsets_from_platformio);
    RUN_TEST(test_dump_flash_parts_has_no_offset_literals);
    RUN_TEST(test_partition_table_still_has_nvs_and_spiffs_rows);
    RUN_TEST(test_assemble_site_chip_family_spelling_is_exact);
    RUN_TEST(test_assemble_site_never_prompts_a_full_erase);
    RUN_TEST(test_assemble_site_validates_no_nvs_overlap);
    RUN_TEST(test_assemble_site_boards_list_matches_ota_manifest_and_board_variant);
    RUN_TEST(test_assemble_site_reuses_the_on_device_ui_assets);
    RUN_TEST(test_installer_page_sets_the_logo_gradient);
    RUN_TEST(test_installer_js_actually_calls_installer_logic);
    RUN_TEST(test_assemble_site_does_not_ship_the_on_device_font_subset);
    RUN_TEST(test_shared_ui_assets_still_match_what_assemble_site_expects);
    RUN_TEST(test_pages_workflow_is_a_reusable_artifact_deploy);
    RUN_TEST(test_pages_workflow_verifies_downloaded_binaries_against_manifest_md5);
    RUN_TEST(test_pages_workflow_resolves_newest_release_by_version_not_date);
    RUN_TEST(test_pages_workflow_verifies_the_deployed_site_answers);
    RUN_TEST(test_release_workflow_chains_pages_on_stable_only);
    RUN_TEST(test_hardware_build_stages_flashparts_after_buildfs);
    RUN_TEST(test_release_reuses_test_jobs_build_artifacts_instead_of_rebuilding);
    RUN_TEST(test_hardware_build_checkout_has_full_history);
    RUN_TEST(test_expected_asset_count_is_derived_not_a_literal);
    RUN_TEST(test_no_code_path_fetches_release_downloads_from_the_browser);
    RUN_TEST(test_esp_web_tools_is_pinned_to_an_exact_version);
    RUN_TEST(test_pages_workflow_never_interpolates_the_tag_directly_into_a_script);
    RUN_TEST(test_installer_js_never_assigns_version_info_via_innerhtml);
    RUN_TEST(test_ci_builds_flashparts_and_assembles_the_site_on_every_push);
    RUN_TEST(test_both_workflows_check_the_assembled_site);
}
