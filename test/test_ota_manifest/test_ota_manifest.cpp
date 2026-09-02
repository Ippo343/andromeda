// Native unit tests for include/ota-manifest.h - the pure JSON half of OTA
// (#63): channel-aware release selection and per-board manifest parsing.
#include <unity.h>

#include <cstdio>
#include <string>

#include "ota-manifest.h"

void setUp() {}
void tearDown() {}

namespace
{
// Trimmed api.github.com "list releases" response: newest first, a
// pre-release on top of two full releases, plus one older release that has
// no manifest.json asset.
const char* kReleaseList = R"json([
  {
    "tag_name": "v0.9-nova-dev", "prerelease": true,
    "assets": [
      {"name": "firmware-esp32_c3_zero-v0.9-nova-dev.bin",
       "browser_download_url": "https://github.com/x/andromeda/releases/download/v0.9-nova-dev/firmware-esp32_c3_zero-v0.9-nova-dev.bin"},
      {"name": "manifest.json",
       "browser_download_url": "https://github.com/x/andromeda/releases/download/v0.9-nova-dev/manifest.json"}
    ]
  },
  {
    "tag_name": "v0.8-clanking-replicator", "prerelease": false,
    "assets": [
      {"name": "manifest.json",
       "browser_download_url": "https://github.com/x/andromeda/releases/download/v0.8-clanking-replicator/manifest.json"}
    ]
  },
  {
    "tag_name": "v0.7-gateway", "prerelease": false,
    "assets": [
      {"name": "manifest.json",
       "browser_download_url": "https://github.com/x/andromeda/releases/download/v0.7-gateway/manifest.json"}
    ]
  }
])json";

// A normal release cycle: six consecutive pre-releases sit on top of the
// newest stable one. A stable-channel device skips all six - so the stable
// release has to still be in the fetched page (per_page=30, not 5) for
// selectRelease to find it. Finding #3.
const char* kLongPrereleaseRun = R"json([
  {"tag_name": "v1.0-rc6", "prerelease": true,
   "assets": [{"name": "manifest.json", "browser_download_url": "https://gh/x/v1.0-rc6/manifest.json"}]},
  {"tag_name": "v1.0-rc5", "prerelease": true,
   "assets": [{"name": "manifest.json", "browser_download_url": "https://gh/x/v1.0-rc5/manifest.json"}]},
  {"tag_name": "v1.0-rc4", "prerelease": true,
   "assets": [{"name": "manifest.json", "browser_download_url": "https://gh/x/v1.0-rc4/manifest.json"}]},
  {"tag_name": "v1.0-rc3", "prerelease": true,
   "assets": [{"name": "manifest.json", "browser_download_url": "https://gh/x/v1.0-rc3/manifest.json"}]},
  {"tag_name": "v1.0-rc2", "prerelease": true,
   "assets": [{"name": "manifest.json", "browser_download_url": "https://gh/x/v1.0-rc2/manifest.json"}]},
  {"tag_name": "v1.0-rc1", "prerelease": true,
   "assets": [{"name": "manifest.json", "browser_download_url": "https://gh/x/v1.0-rc1/manifest.json"}]},
  {"tag_name": "v0.9-stable", "prerelease": false,
   "assets": [{"name": "manifest.json", "browser_download_url": "https://gh/x/v0.9-stable/manifest.json"}]}
])json";

// Same shape but the newest (a full release) is missing its manifest.json.
const char* kReleaseListNewestMissingManifest = R"json([
  {
    "tag_name": "v1.0-broken", "prerelease": false,
    "assets": [
      {"name": "firmware-esp32_c3_zero-v1.0-broken.bin",
       "browser_download_url": "https://github.com/x/andromeda/releases/download/v1.0-broken/firmware-esp32_c3_zero-v1.0-broken.bin"}
    ]
  },
  {
    "tag_name": "v0.9-good", "prerelease": false,
    "assets": [
      {"name": "manifest.json",
       "browser_download_url": "https://github.com/x/andromeda/releases/download/v0.9-good/manifest.json"}
    ]
  }
])json";

const char* kManifest = R"json({
  "channel": "dev",
  "tag": "v0.9-nova-dev",
  "boards": [
    { "board": "esp32_wroom", "versionCode": 460,
      "fw": { "url": "https://cdn/x/firmware-esp32_wroom.bin", "bytes": 1098032, "md5": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" },
      "fs": { "url": "https://cdn/x/littlefs-esp32_wroom.bin", "bytes": 655360, "md5": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb" } },
    { "board": "esp32_c3_zero", "versionCode": 460,
      "fw": { "url": "https://cdn/x/firmware-esp32_c3_zero.bin", "bytes": 1099712, "md5": "cccccccccccccccccccccccccccccccc" },
      "fs": { "url": "https://cdn/x/littlefs-esp32_c3_zero.bin", "bytes": 655360, "md5": "dddddddddddddddddddddddddddddddd" } }
  ]
})json";

// Parse a release-list fixture the way production does: through the shared
// Filter, straight into a StaticJsonDocument.
bool parseList(const char* json, StaticJsonDocument<OtaManifest::RELEASE_LIST_DOC_CAPACITY>& doc)
{
    return deserializeJson(doc, json,
                           DeserializationOption::Filter(OtaManifest::releaseListFilter())) ==
           DeserializationError::Ok;
}
}  // namespace

// ---- parse() -------------------------------------------------------------

void test_parse_picks_the_row_for_this_board()
{
    OtaManifest::Entry e{};
    TEST_ASSERT_TRUE(OtaManifest::parse(kManifest, "esp32_c3_zero", e));

    TEST_ASSERT_EQUAL_STRING("esp32_c3_zero", e.board);
    TEST_ASSERT_EQUAL_UINT32(460, e.versionCode);
    TEST_ASSERT_EQUAL_STRING("v0.9-nova-dev", e.tag);
    TEST_ASSERT_EQUAL_STRING("https://cdn/x/firmware-esp32_c3_zero.bin", e.fwUrl);
    TEST_ASSERT_EQUAL_UINT32(1099712, e.fwBytes);
    TEST_ASSERT_EQUAL_STRING("cccccccccccccccccccccccccccccccc", e.fwMd5);
    TEST_ASSERT_EQUAL_STRING("https://cdn/x/littlefs-esp32_c3_zero.bin", e.fsUrl);
    TEST_ASSERT_EQUAL_UINT32(655360, e.fsBytes);
    TEST_ASSERT_EQUAL_STRING("dddddddddddddddddddddddddddddddd", e.fsMd5);
}

void test_parse_does_not_confuse_boards()
{
    OtaManifest::Entry e{};
    TEST_ASSERT_TRUE(OtaManifest::parse(kManifest, "esp32_wroom", e));
    TEST_ASSERT_EQUAL_STRING("https://cdn/x/firmware-esp32_wroom.bin", e.fwUrl);
}

void test_parse_rejects_unknown_board()
{
    OtaManifest::Entry e{};
    TEST_ASSERT_FALSE(OtaManifest::parse(kManifest, "esp32_s3_zero", e));
}

void test_parse_rejects_malformed_json()
{
    OtaManifest::Entry e{};
    TEST_ASSERT_FALSE(OtaManifest::parse("{not json", "esp32_c3_zero", e));
    TEST_ASSERT_FALSE(OtaManifest::parse("", "esp32_c3_zero", e));
    TEST_ASSERT_FALSE(OtaManifest::parse(nullptr, "esp32_c3_zero", e));
}

void test_parse_rejects_missing_required_fields()
{
    OtaManifest::Entry e{};
    // no versionCode
    TEST_ASSERT_FALSE(OtaManifest::parse(
        R"json({"boards":[{"board":"esp32_c3_zero",
                "fw":{"url":"u","bytes":1,"md5":"m"},"fs":{"url":"u","bytes":1,"md5":"m"}}]})json",
        "esp32_c3_zero", e));
    // no fw.url
    TEST_ASSERT_FALSE(OtaManifest::parse(
        R"json({"boards":[{"board":"esp32_c3_zero","versionCode":1,
                "fw":{"bytes":1,"md5":"m"},"fs":{"url":"u","bytes":1,"md5":"m"}}]})json",
        "esp32_c3_zero", e));
    // zero fs.bytes
    TEST_ASSERT_FALSE(OtaManifest::parse(
        R"json({"boards":[{"board":"esp32_c3_zero","versionCode":1,
                "fw":{"url":"u","bytes":1,"md5":"m"},"fs":{"url":"u","bytes":0,"md5":"m"}}]})json",
        "esp32_c3_zero", e));
}

void test_parse_rejects_oversized_url_instead_of_truncating()
{
    std::string longUrl = "https://cdn/x/";
    longUrl.append(300, 'a');
    longUrl += ".bin";
    std::string json =
        std::string(R"json({"boards":[{"board":"esp32_c3_zero","versionCode":1,)json") +
        R"json("fw":{"url":")json" + longUrl +
        R"json(","bytes":1,"md5":"m"},"fs":{"url":"u","bytes":1,"md5":"m"}}]})json";

    OtaManifest::Entry e{};
    TEST_ASSERT_FALSE(OtaManifest::parse(json.c_str(), "esp32_c3_zero", e));
}

void test_parse_tolerates_missing_top_level_tag()
{
    OtaManifest::Entry e{};
    TEST_ASSERT_TRUE(OtaManifest::parse(
        R"json({"boards":[{"board":"esp32_c3_zero","versionCode":7,
                "fw":{"url":"u","bytes":1,"md5":"0123456789abcdef0123456789abcdef"},
                "fs":{"url":"u","bytes":1,"md5":"fedcba9876543210fedcba9876543210"}}]})json",
        "esp32_c3_zero", e));
    TEST_ASSERT_EQUAL_UINT32(7, e.versionCode);
    TEST_ASSERT_EQUAL_STRING("", e.tag);
}

// An MD5 that isn't exactly 32 lowercase hex chars must sink the row: it's the
// only integrity check on a flashed image, and Update.setMD5() silently turns
// verification *off* for anything else. Bad weather for finding #2.
void test_parse_rejects_a_non_hex_or_wrong_length_md5()
{
    const char* tmpl =
        R"json({"boards":[{"board":"esp32_c3_zero","versionCode":9,
                "fw":{"url":"u","bytes":1,"md5":"%s"},
                "fs":{"url":"u","bytes":1,"md5":"0123456789abcdef0123456789abcdef"}}]})json";
    const char* bad[] = {
        "abc",                                 // too short
        "",                                    // empty
        "0123456789abcdef0123456789abcdef00",  // 34 chars, too long
        "0123456789ABCDEF0123456789ABCDEF",    // uppercase - not what Update emits
        "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz",    // 32 chars, non-hex
        "0123456789abcdef 123456789abcdef",    // embedded space
    };
    for (const char* b : bad)
    {
        char json[400];
        snprintf(json, sizeof(json), tmpl, b);
        OtaManifest::Entry e{};
        TEST_ASSERT_FALSE_MESSAGE(OtaManifest::parse(json, "esp32_c3_zero", e), b);
    }
}

// Good weather: a well-formed 32-hex digest still parses and is copied verbatim.
void test_parse_accepts_a_valid_lowercase_hex_md5()
{
    OtaManifest::Entry e{};
    TEST_ASSERT_TRUE(OtaManifest::parse(
        R"json({"boards":[{"board":"esp32_c3_zero","versionCode":9,
                "fw":{"url":"u","bytes":1,"md5":"0123456789abcdef0123456789abcdef"},
                "fs":{"url":"u","bytes":1,"md5":"fedcba9876543210fedcba9876543210"}}]})json",
        "esp32_c3_zero", e));
    TEST_ASSERT_EQUAL_STRING("0123456789abcdef0123456789abcdef", e.fwMd5);
    TEST_ASSERT_EQUAL_STRING("fedcba9876543210fedcba9876543210", e.fsMd5);
}

// ---- selectRelease() ---------------------------------------------------------

void test_select_stable_skips_prerelease()
{
    StaticJsonDocument<OtaManifest::RELEASE_LIST_DOC_CAPACITY> doc;
    TEST_ASSERT_TRUE(parseList(kReleaseList, doc));

    char url[256] = {};
    TEST_ASSERT_TRUE(OtaManifest::selectRelease(doc, /*devChannel=*/false, url, sizeof(url)));
    TEST_ASSERT_EQUAL_STRING(
        "https://github.com/x/andromeda/releases/download/v0.8-clanking-replicator/manifest.json",
        url);
}

void test_select_dev_takes_newest_even_if_prerelease()
{
    StaticJsonDocument<OtaManifest::RELEASE_LIST_DOC_CAPACITY> doc;
    TEST_ASSERT_TRUE(parseList(kReleaseList, doc));

    char url[256] = {};
    TEST_ASSERT_TRUE(OtaManifest::selectRelease(doc, /*devChannel=*/true, url, sizeof(url)));
    TEST_ASSERT_EQUAL_STRING(
        "https://github.com/x/andromeda/releases/download/v0.9-nova-dev/manifest.json", url);
}

void test_select_rejects_empty_and_non_array()
{
    StaticJsonDocument<OtaManifest::RELEASE_LIST_DOC_CAPACITY> empty;
    TEST_ASSERT_TRUE(parseList("[]", empty));
    char url[64] = {};
    TEST_ASSERT_FALSE(OtaManifest::selectRelease(empty, false, url, sizeof(url)));

    StaticJsonDocument<OtaManifest::RELEASE_LIST_DOC_CAPACITY> obj;
    deserializeJson(obj, "{}");
    TEST_ASSERT_FALSE(OtaManifest::selectRelease(obj, false, url, sizeof(url)));
}

void test_select_does_not_downgrade_when_newest_lacks_manifest()
{
    StaticJsonDocument<OtaManifest::RELEASE_LIST_DOC_CAPACITY> doc;
    TEST_ASSERT_TRUE(parseList(kReleaseListNewestMissingManifest, doc));

    char url[256] = {};
    TEST_ASSERT_FALSE(OtaManifest::selectRelease(doc, false, url, sizeof(url)));
}

void test_select_stable_reaches_past_a_long_run_of_prereleases()
{
    StaticJsonDocument<OtaManifest::RELEASE_LIST_DOC_CAPACITY> doc;
    TEST_ASSERT_TRUE(parseList(kLongPrereleaseRun, doc));

    char url[128] = {};
    // Stable channel: walk past all six -rc tags to the stable release.
    TEST_ASSERT_TRUE(OtaManifest::selectRelease(doc, /*devChannel=*/false, url, sizeof(url)));
    TEST_ASSERT_EQUAL_STRING("https://gh/x/v0.9-stable/manifest.json", url);

    // Dev channel still takes the very newest.
    char devUrl[128] = {};
    TEST_ASSERT_TRUE(OtaManifest::selectRelease(doc, /*devChannel=*/true, devUrl, sizeof(devUrl)));
    TEST_ASSERT_EQUAL_STRING("https://gh/x/v1.0-rc6/manifest.json", devUrl);
}

void test_select_rejects_url_that_does_not_fit()
{
    StaticJsonDocument<OtaManifest::RELEASE_LIST_DOC_CAPACITY> doc;
    TEST_ASSERT_TRUE(parseList(kReleaseList, doc));

    char tiny[16] = {};
    TEST_ASSERT_FALSE(OtaManifest::selectRelease(doc, true, tiny, sizeof(tiny)));
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_picks_the_row_for_this_board);
    RUN_TEST(test_parse_does_not_confuse_boards);
    RUN_TEST(test_parse_rejects_unknown_board);
    RUN_TEST(test_parse_rejects_malformed_json);
    RUN_TEST(test_parse_rejects_missing_required_fields);
    RUN_TEST(test_parse_rejects_oversized_url_instead_of_truncating);
    RUN_TEST(test_parse_tolerates_missing_top_level_tag);
    RUN_TEST(test_parse_rejects_a_non_hex_or_wrong_length_md5);
    RUN_TEST(test_parse_accepts_a_valid_lowercase_hex_md5);
    RUN_TEST(test_select_stable_skips_prerelease);
    RUN_TEST(test_select_dev_takes_newest_even_if_prerelease);
    RUN_TEST(test_select_stable_reaches_past_a_long_run_of_prereleases);
    RUN_TEST(test_select_rejects_empty_and_non_array);
    RUN_TEST(test_select_does_not_downgrade_when_newest_lacks_manifest);
    RUN_TEST(test_select_rejects_url_that_does_not_fit);
    return UNITY_END();
}
