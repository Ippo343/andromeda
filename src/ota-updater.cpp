#include "ota-updater.h"

#include <ArduinoJson.h>
#include <ArduinoLog.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <cstring>

#include "board-variant.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "fs-health.h"
#include "loggers.h"
#include "ota-config.h"
#include "ota-eligibility.h"
#include "ota-manifest.h"
#include "version.h"

namespace OtaUpdater
{
namespace
{
// GitHub "list releases" for this repo, newest first. Unauthenticated: 60
// req/h per IP, far above a daily check. A User-Agent is mandatory or GitHub
// answers 403.
//
// per_page=20 (not 5): a stable-channel device skips every pre-release, so a
// normal -rc/-beta run longer than the page would leave the newest stable
// release off the end of the list and the device reporting "no matching
// release found" until a plain tag lands back inside the window. 20 is still
// a single request, far inside the 60/h budget, and the Filter-reduced list
// fits RELEASE_LIST_DOC_CAPACITY with headroom.
constexpr const char* RELEASES_API_URL =
    "https://api.github.com/repos/Ippo343/andromeda/releases?per_page=20";
constexpr const char* USER_AGENT =
    "andromeda-led-controller (OTA; +https://github.com/Ippo343/andromeda)";

// TLS handshake + JSON/Update buffers need headroom on top of FastLED and the
// async web server. Bail rather than risk an allocation failure mid-flash.
constexpr uint32_t MIN_FREE_HEAP = 60 * 1024;

constexpr TickType_t HTTP_CONNECT_TIMEOUT_MS = 15000;
constexpr TickType_t HTTP_IO_TIMEOUT_MS = 20000;

SemaphoreHandle_t g_lock = nullptr;
Status g_status = {State::Idle, 0, 0, "", ""};

// Single-flight guard. A worker doesn't move g_status.state into a "busy"
// value until it's actually scheduled, so two startCheck()/startUpdate() calls
// in the same tick (or the daily auto-check racing a button press) could both
// get past a state check and spawn a second worker onto the one shared Update
// engine. This flag is claimed synchronously under g_lock before xTaskCreate.
bool g_taskInFlight = false;

// --- status helpers (all take g_lock) ---------------------------------------

void setState(State s)
{
    if (g_lock == nullptr) return;
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_status.state = s;
    xSemaphoreGive(g_lock);
}

void setProgress(uint8_t pct)
{
    if (g_lock == nullptr) return;
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_status.progressPct = pct;
    xSemaphoreGive(g_lock);
}

void fail(const char* msg)
{
    Log.errorln("OTA: %s", msg);
    if (g_lock == nullptr) return;
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_status.state = State::Failed;
    std::strncpy(g_status.error, msg, sizeof(g_status.error) - 1);
    g_status.error[sizeof(g_status.error) - 1] = '\0';
    xSemaphoreGive(g_lock);
}

void recordCheckResult(State s, uint32_t code, const char* tag)
{
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_status.state = s;
    g_status.latestVersionCode = code;
    std::strncpy(g_status.latestTag, tag ? tag : "", sizeof(g_status.latestTag) - 1);
    g_status.latestTag[sizeof(g_status.latestTag) - 1] = '\0';
    g_status.error[0] = '\0';
    xSemaphoreGive(g_lock);
}

// Atomically claim the single-flight slot. Returns false if a check/update
// task is already running (or the lock isn't up yet).
bool claimTask()
{
    if (g_lock == nullptr) return false;
    xSemaphoreTake(g_lock, portMAX_DELAY);
    bool wasFree = !g_taskInFlight;
    if (wasFree) g_taskInFlight = true;
    xSemaphoreGive(g_lock);
    return wasFree;
}

void releaseTask()
{
    if (g_lock == nullptr) return;
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_taskInFlight = false;
    xSemaphoreGive(g_lock);
}

// Evaluate the entry gate (WiFi / heap / single-flight) and log why if it's
// not clear. The pure decision lives in OtaStartGate so the native suite
// covers it; this just samples the live state to feed it.
OtaStartGate::Outcome startOutcome()
{
    bool wifiSta = (WiFi.getMode() & WIFI_MODE_STA) != 0 && WiFi.status() == WL_CONNECTED;

    bool inFlight = true;  // lock not up yet -> treat as busy, never spawn
    if (g_lock != nullptr)
    {
        xSemaphoreTake(g_lock, portMAX_DELAY);
        inFlight = g_taskInFlight;
        xSemaphoreGive(g_lock);
    }

    OtaStartGate::Outcome o =
        OtaStartGate::evaluate(wifiSta, ESP.getFreeHeap(), MIN_FREE_HEAP, inFlight);
    if (o != OtaStartGate::Outcome::Started)
        Log.noticeln("OTA: not starting - %s", OtaStartGate::message(o));
    return o;
}

void configureClient(WiFiClientSecure& client, HTTPClient& http, const char* url)
{
    // #160 tracks replacing this with authenticated TLS. For now the transport
    // is encrypted but not verified; integrity comes from the manifest MD5.
    client.setInsecure();
    http.begin(client, url);
    http.setUserAgent(USER_AGENT);
    http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
    http.setTimeout(HTTP_IO_TIMEOUT_MS);
    // github.com -> objects.githubusercontent.com for asset downloads.
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
}

// --- the actual work ------------------------------------------------------

// Query GitHub, pick the release for our channel, parse its manifest.json,
// and leave the matching board row in `out`. Returns false (and calls fail())
// on any error.
bool fetchLatest(OtaManifest::Entry& out)
{
    WiFiClientSecure client;
    HTTPClient http;
    configureClient(client, http, RELEASES_API_URL);
    http.addHeader("Accept", "application/vnd.github+json");

    int code = http.GET();
    if (code != HTTP_CODE_OK)
    {
        http.end();
        fail("release list request failed");
        return false;
    }

    auto* listDoc = new (std::nothrow) DynamicJsonDocument(OtaManifest::RELEASE_LIST_DOC_CAPACITY);
    if (listDoc == nullptr)
    {
        http.end();
        fail("out of memory for release list");
        return false;
    }
    DeserializationError jsonErr =
        deserializeJson(*listDoc, http.getStream(),
                        DeserializationOption::Filter(OtaManifest::releaseListFilter()));
    http.end();
    if (jsonErr)
    {
        delete listDoc;
        fail("release list JSON parse failed");
        return false;
    }

    char manifestUrl[256];
    bool picked = OtaManifest::selectRelease(*listDoc, OtaConfig::devChannel(), manifestUrl,
                                             sizeof(manifestUrl));
    delete listDoc;
    if (!picked)
    {
        fail("no matching release found");
        return false;
    }

    WiFiClientSecure mClient;
    HTTPClient mHttp;
    configureClient(mClient, mHttp, manifestUrl);
    int mCode = mHttp.GET();
    if (mCode != HTTP_CODE_OK)
    {
        mHttp.end();
        fail("manifest request failed");
        return false;
    }
    String body = mHttp.getString();
    mHttp.end();

    if (!OtaManifest::parse(body.c_str(), BOARD_VARIANT, out))
    {
        fail("manifest has no usable row for this board");
        return false;
    }
    return true;
}

// Stream `url` into the given Update partition (U_FLASH or U_SPIFFS),
// verifying `md5`. `expectedBytes` is the manifest's size for a sanity check.
bool flashFromUrl(const char* url, const char* md5, uint32_t expectedBytes, int command,
                  State writingState)
{
    WiFiClientSecure client;
    HTTPClient http;
    configureClient(client, http, url);

    setState(State::Downloading);
    int code = http.GET();
    if (code != HTTP_CODE_OK)
    {
        http.end();
        fail("image download failed");
        return false;
    }

    int len = http.getSize();
    if (len <= 0)
    {
        http.end();
        fail("image download has no length");
        return false;
    }
    if (expectedBytes != 0 && static_cast<uint32_t>(len) != expectedBytes)
    {
        http.end();
        fail("image size does not match manifest");
        return false;
    }

    if (!Update.begin(static_cast<size_t>(len), command))
    {
        http.end();
        fail(Update.errorString());
        return false;
    }
    // setMD5() returns false and leaves verification *off* unless it's handed
    // exactly 32 chars. OtaManifest::parse already enforces 32 lowercase hex,
    // so this should never fail - but a flashed-but-unverified image is bad
    // enough that we bail rather than trust that invariant held.
    if (!Update.setMD5(md5))
    {
        http.end();
        Update.abort();
        fail("bad md5 in manifest - refusing to flash unverified");
        return false;
    }
    Update.onProgress([](size_t done, size_t total)
                      { setProgress(total ? static_cast<uint8_t>(done * 100 / total) : 0); });

    setProgress(0);  // firmware pass just ended at 100 - restart the bar for this image
    setState(writingState);
    size_t written = Update.writeStream(*http.getStreamPtr());
    http.end();

    if (written != static_cast<size_t>(len))
    {
        Update.abort();
        fail("short write flashing image");
        return false;
    }
    if (!Update.end())
    {
        fail(Update.errorString());  // includes an MD5 mismatch
        return false;
    }
    return true;
}

void checkTask(void*)
{
    setState(State::Checking);

    OtaManifest::Entry latest;
    if (fetchLatest(latest))
    {
        bool newer = latest.versionCode > FIRMWARE_VERSION_CODE;
        recordCheckResult(newer ? State::UpdateAvailable : State::UpToDate, latest.versionCode,
                          latest.tag);
        Log.noticeln("OTA: latest is code %u (%s), running %d - %s", latest.versionCode, latest.tag,
                     FIRMWARE_VERSION_CODE, newer ? "update available" : "up to date");
    }
    releaseTask();
    vTaskDelete(nullptr);
}

void updateTask(void*)
{
    setState(State::Checking);

    OtaManifest::Entry e;
    if (!fetchLatest(e))
    {
        releaseTask();
        vTaskDelete(nullptr);
        return;
    }

    // g_fsDamaged: setup() found LittleFS unmountable (an OTA that lost power
    // mid-write is the likely cause) and reformatted it empty. The firmware is
    // fine but there's no web UI, and `latest > running` would refuse the only
    // update that repairs it - so allow re-flashing the *current* version's
    // filesystem in that one case. Never a downgrade. See ota-eligibility.h.
    const bool fsDamaged = g_fsDamaged;
    if (!OtaEligibility::shouldApply(e.versionCode, FIRMWARE_VERSION_CODE, fsDamaged))
    {
        recordCheckResult(State::UpToDate, e.versionCode, e.tag);
        Log.noticeln("OTA: update requested but already on the newest build");
        releaseTask();
        vTaskDelete(nullptr);
        return;
    }

    // Firmware first: the worst case is new firmware + old assets, which still
    // works (routes are only ever added). FS-first could leave old firmware
    // serving a new index.html whose assets it has no route for. A same-version
    // run is the fs-damage recovery path only - the firmware slot is already
    // correct, so skip straight to the filesystem.
    const bool needFirmware = e.versionCode > FIRMWARE_VERSION_CODE;
    if (needFirmware && !flashFromUrl(e.fwUrl, e.fwMd5, e.fwBytes, U_FLASH, State::WritingFw))
    {
        releaseTask();
        vTaskDelete(nullptr);
        return;
    }

    // Skip the filesystem write when data/ hasn't changed since the last
    // applied update - it's a full-partition transfer + flash-wear + risk
    // window for nothing. Never skip it on the recovery path: the on-flash
    // image is the thing that's broken, and NVS still holds the pre-failure
    // md5 (persistApplied runs only after a successful write).
    char appliedFsMd5[33];
    bool fsUnchanged = !fsDamaged && OtaConfig::appliedFsMd5(appliedFsMd5, sizeof(appliedFsMd5)) &&
                       std::strcmp(appliedFsMd5, e.fsMd5) == 0;
    if (fsUnchanged) { Log.noticeln("OTA: filesystem unchanged (%s) - skipping", e.fsMd5); }
    else
    {
        // The write below overwrites the exact flash region LittleFS has
        // mounted for logging (and everything comms.cpp serves out of it).
        // Unmount first and stop the file logger from touching it - a log
        // line racing the raw write corrupted the fresh image every time
        // (reproduced on hardware: "Corrupted dir pair", failed mount on the
        // next boot) before this guard existed. No LittleFS access of any
        // kind is safe again until the reboot below.
        suspendFileLogging();
        LittleFS.end();

        if (!flashFromUrl(e.fsUrl, e.fsMd5, e.fsBytes, U_SPIFFS, State::WritingFs))
        {
            // Firmware slot is already written and marked bootable; the device
            // will come up on the new firmware with the old filesystem. Acceptable
            // (see above), so record the version and let it reboot rather than
            // leaving it half-applied forever.
            OtaConfig::persistApplied(e.versionCode, "");
            setState(State::Rebooting);
            vTaskDelay(pdMS_TO_TICKS(1500));
            ESP.restart();
            return;
        }
    }

    OtaConfig::persistApplied(e.versionCode, e.fsMd5);
    Log.noticeln("OTA: applied code %u (%s) - rebooting", e.versionCode, e.tag);
    setState(State::Rebooting);
    vTaskDelay(pdMS_TO_TICKS(1500));
    ESP.restart();
}

}  // namespace

void begin()
{
    if (g_lock == nullptr) g_lock = xSemaphoreCreateMutex();
}

OtaStartGate::Outcome startCheck()
{
    OtaStartGate::Outcome o = startOutcome();
    if (o != OtaStartGate::Outcome::Started) return o;
    // claimTask() can still lose to a request that raced us past startOutcome().
    if (!claimTask()) return OtaStartGate::Outcome::Busy;
    if (xTaskCreate(checkTask, "OtaCheck", 10240, nullptr, 1, nullptr) != pdPASS)
    {
        releaseTask();
        return OtaStartGate::Outcome::LowHeap;  // no TCB/stack available
    }
    return OtaStartGate::Outcome::Started;
}

OtaStartGate::Outcome startUpdate()
{
    OtaStartGate::Outcome o = startOutcome();
    if (o != OtaStartGate::Outcome::Started) return o;
    if (!claimTask()) return OtaStartGate::Outcome::Busy;
    if (xTaskCreate(updateTask, "OtaUpdate", 16384, nullptr, 1, nullptr) != pdPASS)
    {
        releaseTask();
        return OtaStartGate::Outcome::LowHeap;
    }
    return OtaStartGate::Outcome::Started;
}

Status status()
{
    if (g_lock == nullptr) return g_status;
    xSemaphoreTake(g_lock, portMAX_DELAY);
    Status copy = g_status;
    xSemaphoreGive(g_lock);
    return copy;
}

bool updateAvailable() { return status().state == State::UpdateAvailable; }

}  // namespace OtaUpdater
