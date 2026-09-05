// Native unit tests for include/ota-manifest.h - the pure JSON half of OTA
// (#63): channel-aware release selection and per-board manifest parsing.
#include <unity.h>

#include <cstdio>
#include <string>

#include "ota-manifest.h"

void test_ota_manifest_setUp() {}
void test_ota_manifest_tearDown() {}

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
// newest stable one. selectRelease() itself still walks past a run of
// pre-releases like this (dev channel would too), even though production no
// longer feeds it a multi-release page for the stable channel - stable now
// gets GitHub's own already-filtered releases/latest instead (see
// LATEST_RELEASE_API_URL in src/ota-updater.cpp). Finding #3.
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

// A realistic 10-asset release body (#162: 3 firmware + 3 littlefs + 3
// flashparts zips + manifest.json), with the same long, codenamed tag shape
// and full github.com/.../releases/download/... URLs a real release actually
// has - not the 1-2 asset toy fixtures above, which are too small to catch a
// capacity regression. Used by both the array-wrapped (dev, per_page=1) and
// bare-object (stable, releases/latest) shapes below.
std::string buildAssets(const std::string& tag)
{
    static const char* kBoards[] = {"esp32_wroom", "esp32_s3_zero", "esp32_c3_zero"};
    std::string base = "https://github.com/Ippo343/andromeda/releases/download/" + tag + "/";
    std::string assets = "[";
    bool first = true;
    for (const char* board : kBoards)
    {
        for (const char* kind : {"firmware", "littlefs"})
        {
            if (!first) assets += ",";
            first = false;
            std::string name = std::string(kind) + "-" + board + "-" + tag + ".bin";
            assets +=
                R"({"name":")" + name + R"(","browser_download_url":")" + base + name + R"("})";
        }
        std::string name = std::string("flashparts-") + board + "-" + tag + ".zip";
        assets += R"(,{"name":")" + name + R"(","browser_download_url":")" + base + name + R"("})";
    }
    assets += R"(,{"name":"manifest.json","browser_download_url":")" + base + R"(manifest.json"}])";
    return assets;
}

// The dev-channel shape: a one-element array, matching per_page=1 on
// RELEASES_API_URL.
std::string buildOneReleasePage(const std::string& tag, bool prerelease)
{
    return R"([{"tag_name":")" + tag + R"(","prerelease":)" + (prerelease ? "true" : "false") +
           R"(,"assets":)" + buildAssets(tag) + "}]";
}

// The stable-channel shape: GET .../releases/latest returns one release
// object directly, not wrapped in an array - and with no "prerelease" field
// needed, since GitHub's own "latest" already excludes drafts/prereleases.
std::string buildLatestRelease(const std::string& tag)
{
    return R"({"tag_name":")" + tag + R"(","assets":)" + buildAssets(tag) + "}";
}

// A single release grown past today's 10 assets, for the headroom probe
// further down: the same real shape plus `extra` more assets on the end.
std::string buildOneReleasePageWithExtraAssets(const std::string& tag, int extra)
{
    std::string base = "https://github.com/Ippo343/andromeda/releases/download/" + tag + "/";
    std::string assets = buildAssets(tag);
    std::string tail;
    for (int i = 0; i < extra; ++i)
    {
        std::string name = "extra-asset-" + std::to_string(i) + "-" + tag + ".bin";
        tail += R"(,{"name":")" + name + R"(","browser_download_url":")" + base + name + R"("})";
    }
    assets.insert(assets.size() - 1, tail);  // before the closing ']'
    return R"([{"tag_name":")" + tag + R"(","prerelease":false,"assets":)" + assets + "}]";
}

// The old misconfiguration this fix replaces: a full page of many releases,
// each with the real 10-asset shape. Reproduces the exact overflow closed by
// cutting RELEASES_API_URL down to per_page=1 - see the capacity constants'
// comments in ota-manifest.h.
std::string buildManyReleasesPage(int count)
{
    std::string out = "[";
    for (int i = 0; i < count; ++i)
    {
        if (i > 0) out += ",";
        std::string tag = "v0." + std::to_string(count - i) + "-some-long-codename-here";
        bool stable = (i == count - 1);  // oldest (last) entry is the stable one
        out += R"({"tag_name":")" + tag + R"(","prerelease":)" + (stable ? "false" : "true") +
               R"(,"assets":)" + buildAssets(tag) + "}";
    }
    out += "]";
    return out;
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

// ---- capacity: a real 10-asset release, in both shapes production uses ----
// (#181-class regression: the release-list buffer used to be sized for the
// 7-asset releases of the time and silently overflowed once #162 grew every
// release to 10 assets - RELEASES_API_URL asked for up to 20 of them in one
// page. selectRelease()/selectFromLatestRelease() only ever need to look at
// the single newest release now (RELEASES_API_URL is per_page=1 for dev,
// LATEST_RELEASE_API_URL is releases/latest for stable), so these prove that
// one real release actually fits - with real headroom, not by accident.)

void test_dev_channel_shape_fits_a_real_release_with_headroom()
{
    std::string json = buildOneReleasePage("v0.9-deep-space-network", /*prerelease=*/false);

    DynamicJsonDocument doc(OtaManifest::RELEASE_LIST_DOC_CAPACITY);
    DeserializationError err =
        deserializeJson(doc, json, DeserializationOption::Filter(OtaManifest::releaseListFilter()));
    TEST_ASSERT_TRUE_MESSAGE(err == DeserializationError::Ok,
                             "a single real 10-asset release must fit RELEASE_LIST_DOC_CAPACITY - "
                             "it no longer needs to hold more than one");
    TEST_ASSERT_TRUE_MESSAGE(doc.memoryUsage() < OtaManifest::RELEASE_LIST_DOC_CAPACITY / 2,
                             "expected real headroom over one release's actual footprint, not a "
                             "capacity sized right up against it");

    char url[256] = {};
    TEST_ASSERT_TRUE(OtaManifest::selectRelease(doc, /*devChannel=*/true, url, sizeof(url)));
    TEST_ASSERT_EQUAL_STRING(
        "https://github.com/Ippo343/andromeda/releases/download/v0.9-deep-space-network/"
        "manifest.json",
        url);
}

void test_stable_channel_shape_fits_a_real_release_with_headroom()
{
    std::string json = buildLatestRelease("v0.9-deep-space-network");

    DynamicJsonDocument doc(OtaManifest::LATEST_RELEASE_DOC_CAPACITY);
    DeserializationError err = deserializeJson(
        doc, json, DeserializationOption::Filter(OtaManifest::latestReleaseFilter()));
    TEST_ASSERT_TRUE_MESSAGE(err == DeserializationError::Ok,
                             "a single real 10-asset releases/latest response must fit "
                             "LATEST_RELEASE_DOC_CAPACITY");
    TEST_ASSERT_TRUE(doc.memoryUsage() < OtaManifest::LATEST_RELEASE_DOC_CAPACITY / 2);

    char url[256] = {};
    TEST_ASSERT_TRUE(OtaManifest::selectFromLatestRelease(doc, url, sizeof(url)));
    TEST_ASSERT_EQUAL_STRING(
        "https://github.com/Ippo343/andromeda/releases/download/v0.9-deep-space-network/"
        "manifest.json",
        url);
}

void test_select_from_latest_release_rejects_non_object_and_missing_assets()
{
    char url[64] = {};

    StaticJsonDocument<64> arr;
    deserializeJson(arr, "[]");
    TEST_ASSERT_FALSE(OtaManifest::selectFromLatestRelease(arr, url, sizeof(url)));

    StaticJsonDocument<64> noAssets;
    deserializeJson(noAssets, R"({"tag_name":"v1"})");
    TEST_ASSERT_FALSE(OtaManifest::selectFromLatestRelease(noAssets, url, sizeof(url)));
}

void test_select_from_latest_release_does_not_find_manifest_among_unrelated_assets()
{
    StaticJsonDocument<256> doc;
    deserializeJson(doc, R"({"assets":[{"name":"firmware-esp32_wroom-v1.bin",)"
                         R"("browser_download_url":"https://x/firmware.bin"}]})");
    char url[64] = {};
    TEST_ASSERT_FALSE(OtaManifest::selectFromLatestRelease(doc, url, sizeof(url)));
}

// Bad weather / regression: the exact old misconfiguration (a 20-release
// page, each with the real 10-asset shape) must NOT be mistaken for something
// that still has to fit - production never requests that page any more
// (RELEASES_API_URL is per_page=1). This pins the actual fix: the capacity
// constants stay sized for one release, not blown out to accommodate many,
// which is the "raise the capacity" alternative this project deliberately
// didn't take (MIN_FREE_HEAP is 60 KB and the TLS session is already open
// when this document is allocated).
// Bad weather / headroom: one release carrying 14 assets - four more than
// today's 10 - must still parse without overflowing. #162 grew a release from
// 7 assets to 10 and silently blew the buffer; this proves the capacity has
// room above today's count for the next such growth, rather than sitting
// exactly on it.
void test_a_release_grown_past_ten_assets_still_fits()
{
    std::string json = buildOneReleasePageWithExtraAssets("v0.9-deep-space-network", /*extra=*/4);

    DynamicJsonDocument doc(OtaManifest::RELEASE_LIST_DOC_CAPACITY);
    DeserializationError err =
        deserializeJson(doc, json, DeserializationOption::Filter(OtaManifest::releaseListFilter()));
    TEST_ASSERT_TRUE_MESSAGE(err == DeserializationError::Ok,
                             "a 14-asset release must still fit RELEASE_LIST_DOC_CAPACITY - the "
                             "capacity must not be sized right up against today's 10 assets");
    TEST_ASSERT_FALSE(doc.overflowed());

    char url[256] = {};
    TEST_ASSERT_TRUE(OtaManifest::selectRelease(doc, /*devChannel=*/true, url, sizeof(url)));
    TEST_ASSERT_EQUAL_STRING(
        "https://github.com/Ippo343/andromeda/releases/download/v0.9-deep-space-network/"
        "manifest.json",
        url);
}

void test_the_old_twenty_release_page_would_still_overflow_the_capacity()
{
    std::string json = buildManyReleasesPage(20);

    DynamicJsonDocument doc(OtaManifest::RELEASE_LIST_DOC_CAPACITY);
    DeserializationError err =
        deserializeJson(doc, json, DeserializationOption::Filter(OtaManifest::releaseListFilter()));
    TEST_ASSERT_TRUE_MESSAGE(
        err == DeserializationError::NoMemory,
        "RELEASE_LIST_DOC_CAPACITY is deliberately sized for one release, not many - if this "
        "starts passing, either the capacity grew far past what a single release needs (risking "
        "the MIN_FREE_HEAP budget) or per_page crept back up from 1, reopening the original bug");
}

void run_test_ota_manifest_tests()
{
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
    RUN_TEST(test_dev_channel_shape_fits_a_real_release_with_headroom);
    RUN_TEST(test_stable_channel_shape_fits_a_real_release_with_headroom);
    RUN_TEST(test_select_from_latest_release_rejects_non_object_and_missing_assets);
    RUN_TEST(test_select_from_latest_release_does_not_find_manifest_among_unrelated_assets);
    RUN_TEST(test_a_release_grown_past_ten_assets_still_fits);
    RUN_TEST(test_the_old_twenty_release_page_would_still_overflow_the_capacity);
}
