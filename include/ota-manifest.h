#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

// Pure JSON helpers for OTA (#63): pick the right GitHub release for a
// device's channel, then pick the right per-board row out of that release's
// manifest.json. No Arduino String, no Stream, no networking - so this whole
// header is exercised by the native unit suite (test/test_ota_manifest),
// while the code that actually opens sockets lives in the non-native
// src/ota-updater.cpp and just calls in here.
//
// Split in two on purpose:
//   selectRelease() takes an already-parsed JsonDocument, so production can
//   stream-parse api.github.com's (tens-of-KB) response straight off the
//   socket with a Filter, while tests parse a fixture string through the
//   identical Filter. Neither path buffers the raw response whole.

namespace OtaManifest
{

// One board's entry from a release manifest.json. Fixed buffers, no heap.
struct Entry
{
    char board[24];
    uint32_t versionCode;
    char tag[48];
    char fwUrl[256];
    uint32_t fwBytes;
    char fwMd5[33];
    char fsUrl[256];
    uint32_t fsBytes;
    char fsMd5[33];
};

// Suggested JsonDocument capacities for callers. Both only ever hold the
// Filter-reduced result (asset name/url, per release), not the raw response.
//
// A release now carries 10 assets (#162: 3 firmware + 3 littlefs + 3
// flashparts zips + manifest.json); measured at ~3 KB filtered for one
// release with realistic (long, codenamed) tag names - see
// test_ota_manifest's capacity-headroom tests. Both capacities keep ~2x
// headroom over that on top of being sized for exactly one release, not many:
// selectRelease() only ever needs to look at the single newest release per
// GET call (see RELEASES_API_URL / LATEST_RELEASE_API_URL in
// src/ota-updater.cpp) - a *previous* version of this asked GitHub for up to
// 20 releases in one page, sized generously enough for the 7-asset releases
// of the time, and silently started failing every check with "release list
// JSON parse failed" once #162 grew each release to 10 assets (measured: even
// 7 releases already overflowed the old 20480 B budget). Both go on the heap
// (DynamicJsonDocument) since the check runs at most once a day, well off any
// hot path; if a filtered response still overruns this, deserializeJson just
// returns NoMemory and the check fails cleanly (no worse than "no release
// found") - it never touches the flash. The per-board manifest is small
// enough for the stack.
constexpr size_t RELEASE_LIST_DOC_CAPACITY = 6144;
constexpr size_t LATEST_RELEASE_DOC_CAPACITY = 6144;
constexpr size_t MANIFEST_DOC_CAPACITY = 3072;

// Filter for the api.github.com "list releases" response - keep only the
// fields selectRelease() looks at. Same Filter for production and tests.
// Capacity is generous: the filter's own nested structure needs more room on
// a 64-bit host (native tests) than on the 32-bit target, and a silently
// overflowed filter would drop "assets" and make every release look empty.
inline StaticJsonDocument<512> releaseListFilter()
{
    // ArduinoJson array-of-objects filter idiom: filter[0] describes every
    // element of the response array; the nested [0] does the same for assets.
    StaticJsonDocument<512> filter;
    filter[0]["tag_name"] = true;
    filter[0]["prerelease"] = true;
    filter[0]["assets"][0]["name"] = true;
    filter[0]["assets"][0]["browser_download_url"] = true;
    return filter;
}

// Filter for GET .../releases/latest - the same per-asset fields as
// releaseListFilter(), but not wrapped in the array-of-releases idiom: this
// endpoint returns one release object directly. No "prerelease"/"tag_name"
// either - GitHub's own "latest" semantics already guarantee this is the
// newest non-draft, non-prerelease release, so selectFromLatestRelease()
// below has no channel logic left to do.
inline StaticJsonDocument<512> latestReleaseFilter()
{
    StaticJsonDocument<512> filter;
    filter["assets"][0]["name"] = true;
    filter["assets"][0]["browser_download_url"] = true;
    return filter;
}

namespace detail
{
// Copy src into dst[dstCap], rejecting (false) a value that doesn't fit or is
// missing - a truncated URL must never be acted on.
inline bool copyField(char* dst, size_t dstCap, const char* src)
{
    if (src == nullptr) return false;
    size_t n = std::strlen(src);
    if (n == 0 || n >= dstCap) return false;
    std::memcpy(dst, src, n + 1);
    return true;
}

// An MD5 hex digest is exactly 32 lowercase hex characters. This is the only
// integrity check on a flashed image (the transport is TLS but unverified,
// #160), and Update.setMD5() silently disables verification unless it's given
// exactly 32 chars - so a manifest row whose md5 is any other length or
// contains a non-hex character must be rejected here, before it can reach
// Update, rather than flashing an unverified image.
inline bool isMd5Hex(const char* s)
{
    if (s == nullptr) return false;
    size_t n = 0;
    for (; s[n] != '\0'; ++n)
    {
        char c = s[n];
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!hex || n >= 32) return false;
    }
    return n == 32;
}
}  // namespace detail

// From a parsed (Filter-reduced) api.github.com release list - newest first -
// write the manifest.json download URL of the release this channel should
// track into outUrl[outCap].
//   stable: newest release with prerelease == false
//   dev:    newest release, period (so a dev unit still takes a newer stable)
// Returns false if the doc isn't a non-empty array, no release matches, the
// chosen release has no manifest.json asset, or the URL doesn't fit.
inline bool selectRelease(const JsonDocument& releaseList, bool devChannel, char* outUrl,
                          size_t outCap)
{
    if (!releaseList.is<JsonArrayConst>()) return false;
    JsonArrayConst releases = releaseList.as<JsonArrayConst>();
    if (releases.size() == 0) return false;

    for (JsonObjectConst rel : releases)
    {
        if (!devChannel && rel["prerelease"].as<bool>()) continue;

        for (JsonObjectConst asset : rel["assets"].as<JsonArrayConst>())
        {
            const char* name = asset["name"].as<const char*>();
            if (name != nullptr && std::strcmp(name, "manifest.json") == 0)
                return detail::copyField(outUrl, outCap,
                                         asset["browser_download_url"].as<const char*>());
        }
        // The newest release for this channel has no manifest.json - don't
        // fall through to an older one, that would silently downgrade.
        return false;
    }
    return false;
}

// From a parsed (Filter-reduced) GET .../releases/latest response - a single
// release object, not a list - write its manifest.json download URL into
// outUrl[outCap]. The stable-channel counterpart to selectRelease(): GitHub's
// "latest" already means the newest non-draft, non-prerelease release, so
// there's no list to walk and no prerelease check to make, only the same
// manifest.json lookup selectRelease() does per-release. Returns false if the
// doc isn't an object, has no "assets" array, has no manifest.json asset, or
// the URL doesn't fit - never falls back to an older release, same as
// selectRelease().
inline bool selectFromLatestRelease(const JsonDocument& release, char* outUrl, size_t outCap)
{
    if (!release.is<JsonObjectConst>()) return false;
    JsonObjectConst rel = release.as<JsonObjectConst>();
    if (!rel.containsKey("assets")) return false;

    for (JsonObjectConst asset : rel["assets"].as<JsonArrayConst>())
    {
        const char* name = asset["name"].as<const char*>();
        if (name != nullptr && std::strcmp(name, "manifest.json") == 0)
            return detail::copyField(outUrl, outCap,
                                     asset["browser_download_url"].as<const char*>());
    }
    return false;
}

// Pull the row for `board` out of a release's manifest.json:
//   { "channel": "...", "tag": "...", "boards": [
//       { "board": "...", "versionCode": N,
//         "fw": { "url": "...", "bytes": N, "md5": "..." },
//         "fs": { "url": "...", "bytes": N, "md5": "..." } }, ... ] }
// Returns false on parse error, no matching board, or any missing/oversized
// required field.
inline bool parse(const char* manifestJson, const char* board, Entry& out)
{
    if (manifestJson == nullptr || board == nullptr) return false;

    StaticJsonDocument<MANIFEST_DOC_CAPACITY> doc;
    if (deserializeJson(doc, manifestJson) != DeserializationError::Ok) return false;

    const char* tag = doc["tag"].as<const char*>();

    for (JsonObjectConst row : doc["boards"].as<JsonArrayConst>())
    {
        const char* rowBoard = row["board"].as<const char*>();
        if (rowBoard == nullptr || std::strcmp(rowBoard, board) != 0) continue;

        if (!row.containsKey("versionCode")) return false;
        JsonObjectConst fw = row["fw"];
        JsonObjectConst fs = row["fs"];

        Entry e{};
        e.versionCode = row["versionCode"].as<uint32_t>();
        e.fwBytes = fw["bytes"].as<uint32_t>();
        e.fsBytes = fs["bytes"].as<uint32_t>();

        // tag is display-only; tolerate its absence.
        if (tag != nullptr && std::strlen(tag) < sizeof(e.tag)) std::strcpy(e.tag, tag);

        const char* fwMd5 = fw["md5"].as<const char*>();
        const char* fsMd5 = fs["md5"].as<const char*>();
        if (!detail::isMd5Hex(fwMd5) || !detail::isMd5Hex(fsMd5)) return false;

        bool ok = detail::copyField(e.board, sizeof(e.board), rowBoard) &&
                  detail::copyField(e.fwUrl, sizeof(e.fwUrl), fw["url"].as<const char*>()) &&
                  detail::copyField(e.fwMd5, sizeof(e.fwMd5), fwMd5) &&
                  detail::copyField(e.fsUrl, sizeof(e.fsUrl), fs["url"].as<const char*>()) &&
                  detail::copyField(e.fsMd5, sizeof(e.fsMd5), fsMd5);
        if (!ok || e.fwBytes == 0 || e.fsBytes == 0) return false;

        out = e;
        return true;
    }
    return false;
}

}  // namespace OtaManifest
