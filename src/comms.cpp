#include "comms.h"

#include <DNSServer.h>
#include <esp_system.h>
#include <sys/stat.h>

#include <cstring>

#include "ota-config.h"
#include "ota-updater.h"
#include "version.h"
#include "ws-command-parser.h"

constexpr int DNS_PORT = 53;

// Silent "does this file exist" check. LittleFSImpl::exists() (and the
// AsyncFileResponse path behind request->send(LittleFS, ...)) is just
// open(path, "r"), which logs a VFS error at level E for every miss - and
// the Advanced page polls the log routes every 2s, so a not-yet-rotated
// /log1.txt would spew "does not exist" lines forever. A bare stat() on the
// mount path checks without opening. LittleFS.begin() (main.cpp) takes the
// default "/littlefs" mount point.
static bool fileExistsQuiet(const char* littleFsPath)
{
    char full[40];
    snprintf(full, sizeof(full), "/littlefs%s", littleFsPath);
    struct stat st;
    return stat(full, &st) == 0;
}

// OtaUpdater::State -> the lowercase token the Advanced page's poll loop
// switches on. Kept here (not in ota-updater.h) so the enum stays an
// implementation detail of the updater.
static const char* otaStateToken(OtaUpdater::State s)
{
    switch (s)
    {
        case OtaUpdater::State::Idle:
            return "idle";
        case OtaUpdater::State::Checking:
            return "checking";
        case OtaUpdater::State::UpToDate:
            return "uptodate";
        case OtaUpdater::State::UpdateAvailable:
            return "available";
        case OtaUpdater::State::Downloading:
            return "downloading";
        case OtaUpdater::State::WritingFw:
            return "writing-fw";
        case OtaUpdater::State::WritingFs:
            return "writing-fs";
        case OtaUpdater::State::Rebooting:
            return "rebooting";
        case OtaUpdater::State::Failed:
            return "failed";
    }
    return "idle";
}

// Serves a LittleFS text file if present, else an empty 200 - callers are
// the log routes, where "file not created yet" is normal and the Advanced
// page treats an empty body as an empty log.
static void serveTextFileOrEmpty(AsyncWebServerRequest* r, const char* path)
{
    if (fileExistsQuiet(path))
        r->send(LittleFS, path, "text/plain");
    else
        r->send(200, "text/plain", "");
}

// Narrow stopgap against the classic drive-by CSRF one-click case: a foreign page's
// <form> auto-submitted from a browser on the same LAN, targeting POST /save or /reset.
// Neither route requires a CORS preflight (simple form POSTs never trigger one), so
// nothing else here would ever see or block it. A cross-origin browser POST always
// carries an Origin header that won't match this device's own host; a request with none
// at all (curl, a script, same-origin fetch from this device's own pages) is let
// through unchanged. There's no other auth on this device at all (#85, tracked
// separately) - this only closes the "just visiting an unrelated web page" case, not a
// deliberate attacker who already knows the device's address.
static bool isCrossOriginPost(AsyncWebServerRequest* request)
{
    if (!request->hasHeader("Origin")) return false;
    const AsyncWebHeader* origin = request->getHeader("Origin");
    return !originMatchesHost(origin->value().c_str(), request->host().c_str());
}

#define STATIC_FILE_ROUTE(path, contentType) \
    server.on(path, HTTP_GET,                \
              [](AsyncWebServerRequest* request) { request->send(LittleFS, path, contentType); })

Comms::Comms()
    : server(80),
      ws("/ws"),
      webServerTaskHandle(nullptr),
      dnsServer(nullptr),
      isAPMode(false),
      realWifiManager(wifiConnector, preferencesStore),
      wifiManager(&realWifiManager),
      scanInProgress(false),
      scanComplete(false),
      scanResults(""),
      scanResultsMutex(xSemaphoreCreateMutex()),
      lastScanTime(0),
      lastBroadcastMs(0)
{
}

Comms::SetupOutcome Comms::setup()
{
    WifiManager::ConnectResult result = wifiManager->connectUsingStoredCredentials();
    if (result == WifiManager::ConnectResult::Connected)
    {
        startStationMode();
        return SetupOutcome::Connected;
    }

    Log.noticeln("Starting AP mode for WiFi configuration");
    startAPMode();
    return result == WifiManager::ConnectResult::NeverConfigured ? SetupOutcome::NeverConfigured
                                                                 : SetupOutcome::ConnectFailed;
}

// Shared by startAPMode() (boot-time fallback) and enterAPFallbackMode() (runtime fallback
// after a mid-run disconnect): flips the radio into AP mode and (re)starts the captive-portal
// DNS server. Does not touch the web server task - see enterAPFallbackMode()'s comment.
void Comms::beginAPBroadcast()
{
    // Computed/allocated/started before the lock is taken - see apStateMux's comment in
    // comms.h for why nothing that can allocate or block runs while holding it.
    String newName = DeviceIdentity::getDeviceName();
    wifiConnector.enterAPMode();

    IPAddress apIP = WiFi.softAPIP();
    Log.noticeln("AP Mode started. IP: %s", apIP.toString().c_str());

    DNSServer* newDns = new DNSServer();
    newDns->start(DNS_PORT, "*", apIP);

    portENTER_CRITICAL(&apStateMux);
    DNSServer* oldDns = dnsServer;
    dnsServer = newDns;
    isAPMode = true;
    strncpy(runningDeviceName, newName.c_str(), sizeof(runningDeviceName) - 1);
    runningDeviceName[sizeof(runningDeviceName) - 1] = '\0';
    portEXIT_CRITICAL(&apStateMux);

    // beginAPBroadcast() only ever runs once per boot in practice (isAPMode has no path
    // back to false without a reboot, and enterAPFallbackMode() early-returns once already
    // in AP mode), so oldDns is always nullptr here - deleted defensively anyway, and
    // outside the lock, in case that invariant ever changes.
    delete oldDns;

    // Covers both ways the device ends up here: a failed boot-time join
    // (Comms::setup() -> startAPMode()) and a mid-run outage that outlasted
    // enterAPFallbackMode()'s dead time - either way, this is what stops it being a
    // one-way door into the setup AP.
    startApRejoinMonitor();
}

bool Comms::isInAPMode() const
{
    portENTER_CRITICAL(&apStateMux);
    bool result = isAPMode;
    portEXIT_CRITICAL(&apStateMux);
    return result;
}

bool Comms::startAPMode()
{
    beginAPBroadcast();
    createWebServerTask();

    // Background scan for setup UI
    xTaskCreate(
        [](void* p)
        {
            vTaskDelay(2000 / portTICK_PERIOD_MS);
            Comms::Instance().startAsyncScan();
            vTaskDelete(NULL);
        },
        "InitScan", 2048, nullptr, 1, nullptr);

    return true;
}

void Comms::enterAPFallbackMode()
{
    if (isAPMode) return;

    Log.warningln("WiFi unreachable for too long, falling back to AP mode for reconfiguration");
    beginAPBroadcast();

    // The web server task is already running (createWebServerTask() ran at boot in
    // startStationMode()) and its loop already checks isAPMode/dnsServer every tick to decide
    // whether to service the captive portal - nothing else to start. Kick a background scan
    // so the setup page has networks to show as soon as someone connects to the AP.
    xTaskCreate(
        [](void* p)
        {
            vTaskDelay(2000 / portTICK_PERIOD_MS);
            Comms::Instance().startAsyncScan();
            vTaskDelete(NULL);
        },
        "APFallbackScan", 2048, nullptr, 1, nullptr);
}

bool Comms::startStationMode()
{
    // No lock needed here: this only ever runs at boot, before the web server task (the
    // only concurrent reader of these fields) exists - unlike beginAPBroadcast(), which
    // is also reachable at runtime from the WifiRecovery monitor task.
    isAPMode = false;
    String newName = DeviceIdentity::getDeviceName();
    strncpy(runningDeviceName, newName.c_str(), sizeof(runningDeviceName) - 1);
    runningDeviceName[sizeof(runningDeviceName) - 1] = '\0';

    // mDNS is the primary way to reach the device now that station mode uses DHCP instead of
    // a fixed IP (see EspWiFiConnector::connect()) - worth a couple of retries rather than
    // giving up on the first transient failure and leaving the device unreachable by name for
    // the rest of the boot. Runs before the web server task exists, so blocking briefly here
    // is harmless (same reasoning as WifiManager's connect()).
    bool mdnsStarted = false;
    for (int attempt = 0; !mdnsStarted && attempt < 3; attempt++)
    {
        if (attempt > 0) delay(200);
        mdnsStarted = MDNS.begin(DeviceIdentity::getMdnsHostname().c_str());
    }
    if (!mdnsStarted) Log.errorln("MDNS failed to start after retries");

    createWebServerTask();
    printWifiStatus();
    return true;
}

void Comms::createWebServerTask()
{
    xTaskCreatePinnedToCore(webServerTask, "WebServer", 16384, this, 1, &webServerTaskHandle, 0);
}

void Comms::webServerTask(void* parameter)
{
    Comms* comms = static_cast<Comms*>(parameter);
    comms->setupRoutes();
    comms->server.begin();
    Log.noticeln("Web server started on Core %d", xPortGetCoreID());

    while (true)
    {
        // Snapshot both fields under the lock, then act on the snapshot outside it - see
        // apStateMux's comment in comms.h. dnsServer is never deleted with a reader
        // possibly still using it in practice (beginAPBroadcast() only runs once per
        // boot), so holding a raw pointer past the lock here is safe.
        portENTER_CRITICAL(&comms->apStateMux);
        bool apMode = comms->isAPMode;
        DNSServer* dns = comms->dnsServer;
        portEXIT_CRITICAL(&comms->apStateMux);
        if (apMode && dns) { dns->processNextRequest(); }

        comms->broadcastStateIfDirty();
        // Bounds the connected-client list to DEFAULT_MAX_WS_CLIENTS - previously never
        // called anywhere, so nothing pruned stale/excess clients and each one holds its own
        // send queue of multi-KB state JSON on a device with no PSRAM.
        comms->ws.cleanupClients();
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void Comms::setupRoutes()
{
    // WebSocket endpoint
    ws.onEvent(
        [](AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg,
           uint8_t* data, size_t len)
        {
            if (type == WS_EVT_CONNECT)
            {
                // The WebSocket handshake is a plain GET, so a browser on an
                // unrelated LAN-reachable page can open ws://<device>/ws with
                // no CORS preflight and then send {"type":"reboot"} etc. -
                // bypassing every isCrossOriginPost() guard on the HTTP
                // routes. Reject a handshake whose Origin doesn't match this
                // device's own host. For WS_EVT_CONNECT the event arg is the
                // upgrade request; a request with no Origin (a native client,
                // a same-origin page) is allowed, same policy as the POST
                // routes.
                auto* req = reinterpret_cast<AsyncWebServerRequest*>(arg);
                if (req && req->hasHeader("Origin") &&
                    !originMatchesHost(req->getHeader("Origin")->value().c_str(),
                                       req->host().c_str()))
                {
                    Log.warningln("Rejecting cross-origin WebSocket handshake");
                    client->close();
                    return;
                }

                // Auto-ping every 30s of silence. Without this, a WiFi drop that doesn't
                // produce a clean TCP FIN (the normal case for the device losing power or
                // moving out of range - not a client-side "close the tab") leaves the
                // client's readyState stuck at OPEN for minutes: no error/close event fires,
                // the UI never shows as disconnected, and every button press silently does
                // nothing. This makes the library itself notice a dead peer (a failed/
                // unacked ping closes the connection) instead of relying only on the
                // client-side staleness timer in controls.js.
                client->keepAlivePeriod(30);

                char buf[WsStateBuilder::JSON_CAPACITY];
                size_t stateLen = Comms::Instance().buildCurrentStateJson(buf, sizeof(buf));
                if (stateLen) client->text(buf, stateLen);
            }
            else if (type == WS_EVT_DATA)
            {
                AwsFrameInfo* info = (AwsFrameInfo*)arg;
                if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
                {
                    // Copy into a bounded, NUL-terminated local buffer. `data`
                    // points into the lwIP pbuf payload, so the previous
                    // `data[len] = 0` wrote one byte past it. Every real
                    // command message is well under this; a longer frame
                    // isn't one and is dropped.
                    char json[256];
                    if (len >= sizeof(json))
                    {
                        Log.warningln("Ignoring oversized WS message (%u bytes)",
                                      static_cast<unsigned>(len));
                        return;
                    }
                    memcpy(json, data, len);
                    json[len] = 0;

                    MissionControl& mc = MissionControl::Instance();

                    // BRIGHTNESS and live COLOR updates fire at drag speed (dozens/sec) -
                    // routing every one through the command queue overruns it and starves
                    // async_tcp with per-command logging. Apply them directly; they're simple
                    // field writes the render task re-reads every frame, same as before
                    // commands were queue-only. Everything else stays queued since it mutates
                    // render-task-owned state (allocating effects, etc.).
                    uint8_t brightnessValue;
                    bool brightnessCommit;
                    Command command;
                    if (WsCommandParser::parseBrightness(json, brightnessValue, brightnessCommit))
                    {
                        mc.setMaxBrightness(brightnessValue);
                        if (brightnessCommit) BrightnessConfig::persist(brightnessValue);
                    }
                    else if (WsCommandParser::parse(json, command))
                    {
                        if (command.type == CommandType::COLOR && mc.isColorActive())
                        {
                            mc.setLiveColor(command.r, command.g, command.b);
                            // Same commit-gating as BRIGHTNESS above - only the drag-release
                            // message persists. This is the common case for a color commit:
                            // by the time a drag ends, color mode is already active, so the
                            // message never touches the queue path below (which handles the
                            // same flag for the rarer case of a commit landing on the very
                            // message that first switches into color mode).
                            if (command.colorCommit)
                                StartupStateConfig::persistMode(
                                    StartupStateConfig::Mode::HoldingColor, 0, command.r, command.g,
                                    command.b);
                        }
                        else
                            mc.queueWebCommand(command);
                    }
                    else
                    {
                        // ArduinoLog's format strings only support a single character after
                        // '%' (no printf-style width/precision - see ArduinoLog.cpp's
                        // printFormat()), so truncating has to happen on the string itself
                        // before logging it, not via the format spec. Bounded because `json`
                        // is an unvalidated, client-controlled message written into the 32KB
                        // rotating log - previously unbounded, so a client (accidentally or
                        // not - no auth on this route) sending large garbage frames could
                        // flush the real diagnostic history in a handful of messages.
                        char truncated[129];
                        snprintf(truncated, sizeof(truncated), "%s", json);
                        Log.warningln("Unrecognized WS command message: %s", truncated);
                    }
                }
                else
                {
                    // Multi-packet/fragmented frames (info->index != 0, or a partial
                    // info->len != len) and non-text opcodes previously hit neither branch -
                    // silently dropped with not even a log line. This client only ever sends
                    // small single-frame messages, so it's not expected in practice, but a
                    // proxy or a future non-firmware client fragmenting a message deserves a
                    // trace instead of vanishing without one.
                    Log.warningln(
                        "Dropped WS frame (final=%d index=%u len=%u opcode=%d) - fragmented "
                        "or non-text frames aren't supported",
                        (int)info->final, (unsigned long)info->index, (unsigned long)len,
                        (int)info->opcode);
                }
            }
        });
    server.addHandler(&ws);

    // Global static files
    server.on("/", HTTP_GET,
              [](AsyncWebServerRequest* r) { r->send(LittleFS, "/index.html", "text/html"); });

    // TODO: I am confident there must exist a better way.
    // Virtually certain the ESPAsyncWebServer can serve directly from LittleFS with the correct
    // content type
    STATIC_FILE_ROUTE("/index.html", "text/html");
    STATIC_FILE_ROUTE("/wifi-setup.html", "text/html");
    STATIC_FILE_ROUTE("/favicon.svg", "image/svg+xml");
    STATIC_FILE_ROUTE("/fonts/cinzel.woff2", "font/woff2");
    STATIC_FILE_ROUTE("/js/utils.js", "application/javascript");
    STATIC_FILE_ROUTE("/js/ws-client-utils.js", "application/javascript");
    STATIC_FILE_ROUTE("/js/controls-logic.js", "application/javascript");
    STATIC_FILE_ROUTE("/js/controls.js", "application/javascript");
    STATIC_FILE_ROUTE("/js/wifi.js", "application/javascript");
    STATIC_FILE_ROUTE("/js/advanced-logic.js", "application/javascript");
    STATIC_FILE_ROUTE("/js/advanced.js", "application/javascript");
    STATIC_FILE_ROUTE("/js/device-name.js", "application/javascript");
    STATIC_FILE_ROUTE("/css/common.css", "text/css");
    STATIC_FILE_ROUTE("/css/controls.css", "text/css");
    STATIC_FILE_ROUTE("/css/wifi.css", "text/css");
    STATIC_FILE_ROUTE("/css/advanced.css", "text/css");
    STATIC_FILE_ROUTE("/advanced.html", "text/html");
    STATIC_FILE_ROUTE("/device-name.html", "text/html");

    server.on("/wifi", HTTP_GET,
              [](AsyncWebServerRequest* r) { r->send(LittleFS, "/wifi-setup.html", "text/html"); });

    // Log files, served from LittleFS via serveTextFileOrEmpty so a
    // not-yet-created file returns an empty 200 instead of letting the FS
    // layer log a VFS error on the miss (see fileExistsQuiet above). The
    // logger keeps the log as these two rotating files; /log1.txt has no
    // content until the first 32 KB rotation. Keep the literals in sync with
    // LOG_FILE_CUR / LOG_FILE_OLD (utils.h).
    server.on("/log0.txt", HTTP_GET,
              [](AsyncWebServerRequest* r) { serveTextFileOrEmpty(r, LOG_FILE_CUR); });
    server.on("/log1.txt", HTTP_GET,
              [](AsyncWebServerRequest* r) { serveTextFileOrEmpty(r, LOG_FILE_OLD); });
    server.on("/logs", HTTP_GET,
              [](AsyncWebServerRequest* r) { serveTextFileOrEmpty(r, LOG_FILE_CUR); });

    server.on(
        "/metrics", HTTP_GET,
        [](AsyncWebServerRequest* r)
        {
            // fps() is NaN before the first frame, and temperatureRead() can be
            // NaN on a bad/early sensor read - both must serialize as JSON null,
            // not the bare token "nan" which would make the whole payload
            // unparseable. Self-comparison is the header-free NaN test.
            float fps = PerformanceMonitor::Instance().fps();
            char fpsBuf[16];
            if (fps != fps) { strcpy(fpsBuf, "null"); }
            else { snprintf(fpsBuf, sizeof(fpsBuf), "%.1f", fps); }

            float tempC = temperatureRead();
            char tempBuf[16];
            if (tempC != tempC) { strcpy(tempBuf, "null"); }
            else { snprintf(tempBuf, sizeof(tempBuf), "%.1f", tempC); }

            // VERSION is the build-time git describe string from generate_version.py
            // (build-scripts/inject_version.py). It only ever contains chars that are
            // safe unescaped inside a JSON string (word chars, '.', '-', ' ', '(', ')'
            // and '/' from a branch name) - no quote or backslash is reachable - so it
            // is interpolated directly.
            // latestTag, like VERSION, is a git tag name from our own releases
            // (v0.9-word-word[-dev]) - no quote/backslash reachable - so it's
            // interpolated directly too.
            OtaUpdater::Status ota = OtaUpdater::status();

            char json[640];
            snprintf(json, sizeof(json),
                     "{\"uptimeMs\":%lu,\"heapFree\":%u,\"heapMin\":%u,\"heapTotal\":%u,"
                     "\"tempC\":%s,\"fps\":%s,\"rssi\":%d,\"cpuMhz\":%u,"
                     "\"chip\":\"%s\",\"resetReason\":%d,\"version\":\"%s\","
                     "\"updateAvailable\":%s,\"latestTag\":\"%s\",\"otaChannel\":\"%s\"}",
                     static_cast<unsigned long>(millis()), static_cast<unsigned>(ESP.getFreeHeap()),
                     static_cast<unsigned>(ESP.getMinFreeHeap()),
                     static_cast<unsigned>(ESP.getHeapSize()), tempBuf, fpsBuf,
                     static_cast<int>(WiFi.RSSI()), static_cast<unsigned>(ESP.getCpuFreqMHz()),
                     ESP.getChipModel(), static_cast<int>(esp_reset_reason()), VERSION,
                     OtaUpdater::updateAvailable() ? "true" : "false", ota.latestTag,
                     OtaConfig::devChannel() ? "dev" : "stable");
            r->send(200, "application/json", json);
        });

    // Shared Config & Monitoring
    server.on("/fps", HTTP_GET, [](AsyncWebServerRequest* r)
              { r->send(200, "text/plain", String(PerformanceMonitor::Instance().fps())); });
    server.on(
        "/brightness", HTTP_GET, [](AsyncWebServerRequest* r)
        { r->send(200, "text/plain", String(MissionControl::Instance().getMaxBrightness())); });

    // WiFi functionality
    server.on("/scan", HTTP_GET, [this](AsyncWebServerRequest* r)
              { r->send(200, "application/json", scanWiFiNetworks()); });
    server.on("/save", HTTP_POST,
              [this](AsyncWebServerRequest* r)
              {
                  if (isCrossOriginPost(r))
                  {
                      r->send(403, "text/plain", "Cross-origin request rejected");
                      return;
                  }
                  processWiFiCredentials(r, r->arg("ssid"), r->arg("password"));
              });
    // Polled by the setup page after a 202 from /save, until it reads a terminal verdict.
    // "pending" covers Idle too (the page only polls once a probe is in flight).
    server.on("/save-status", HTTP_GET,
              [this](AsyncWebServerRequest* r)
              {
                  const char* status = "pending";
                  if (saveStatus == SaveStatus::Connected)
                      status = "connected";
                  else if (saveStatus == SaveStatus::Failed)
                      status = "failed";
                  r->send(200, "text/plain", status);
              });
    server.on("/reset", HTTP_POST,
              [this](AsyncWebServerRequest* r)
              {
                  if (isCrossOriginPost(r))
                  {
                      r->send(403, "text/plain", "Cross-origin request rejected");
                      return;
                  }
                  wifiManager->clearStoredCredentials();
                  r->send(200, "text/plain", "Credentials cleared. Restarting...");
                  xTaskCreate(
                      [](void*)
                      {
                          vTaskDelay(2000 / portTICK_PERIOD_MS);
                          ESP.restart();
                      },
                      "Restart", 2048, NULL, 1, NULL);
              });

    // OTA (#63). One-shot triggers return 202 like /save; the Advanced page
    // polls GET /ota-status. All three POSTs get the same drive-by-CSRF guard
    // as /save and /reset.
    server.on("/ota", HTTP_POST,
              [](AsyncWebServerRequest* r)
              {
                  if (isCrossOriginPost(r))
                  {
                      r->send(403, "text/plain", "Cross-origin request rejected");
                      return;
                  }
                  // Answer with what actually happened: 202 only if a worker
                  // was spawned, 409/503 (with a reason) otherwise, so the
                  // Advanced page never polls a request that did nothing.
                  OtaStartGate::Outcome o = OtaUpdater::startUpdate();
                  r->send(OtaStartGate::httpStatus(o), "text/plain", OtaStartGate::message(o));
              });
    server.on("/ota-check", HTTP_POST,
              [](AsyncWebServerRequest* r)
              {
                  if (isCrossOriginPost(r))
                  {
                      r->send(403, "text/plain", "Cross-origin request rejected");
                      return;
                  }
                  OtaStartGate::Outcome o = OtaUpdater::startCheck();
                  r->send(OtaStartGate::httpStatus(o), "text/plain", OtaStartGate::message(o));
              });
    server.on("/ota-channel", HTTP_POST,
              [](AsyncWebServerRequest* r)
              {
                  if (isCrossOriginPost(r))
                  {
                      r->send(403, "text/plain", "Cross-origin request rejected");
                      return;
                  }
                  bool dev = r->arg("dev") == "true" || r->arg("dev") == "1";
                  OtaConfig::persistDevChannel(dev);
                  OtaUpdater::startCheck();  // re-evaluate against the new channel
                  r->send(200, "text/plain", dev ? "dev" : "stable");
              });
    server.on("/ota-status", HTTP_GET,
              [](AsyncWebServerRequest* r)
              {
                  OtaUpdater::Status s = OtaUpdater::status();
                  // Worst case is ~250 B (error[96] + latestTag[48] both full);
                  // 320 keeps a rare long Update error string from truncating the JSON.
                  char json[320];
                  snprintf(json, sizeof(json),
                           "{\"state\":\"%s\",\"progress\":%u,\"latestCode\":%u,"
                           "\"latestTag\":\"%s\",\"channel\":\"%s\",\"error\":\"%s\"}",
                           otaStateToken(s.state), static_cast<unsigned>(s.progressPct),
                           static_cast<unsigned>(s.latestVersionCode), s.latestTag,
                           OtaConfig::devChannel() ? "dev" : "stable", s.error);
                  r->send(200, "application/json", json);
              });

    // Captive Portal Detection
    server.on("/generate_204", HTTP_GET, [this](AsyncWebServerRequest* r) { r->redirect("/"); });
    server.on("/hotspot-detect.html", HTTP_GET,
              [this](AsyncWebServerRequest* r) { r->redirect("/"); });

    server.onNotFound(
        [this](AsyncWebServerRequest* request)
        {
            if (isInAPMode() && request->host() != WiFi.softAPIP().toString())
            {
                request->redirect("http://" + WiFi.softAPIP().toString() + "/");
            }
            else { request->send(404, "text/plain", "Not Found"); }
        });
}

void Comms::onWiFiScanComplete(int networksFound)
{
    String json = "{\"networks\":[";
    for (size_t i = 0; i < networksFound; i++)
    {
        if (i > 0) json += ",";
        // wifi.js has always read network.encryption (to pick the lock-icon and to
        // autofocus the password field for a non-open network) but /scan never actually
        // sent it - every network showed the literal text "undefined" on the setup page.
        const char* encryption = WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "none" : "secured";
        json += "{\"ssid\":\"" + jsonEscape(WiFi.SSID(i)) + "\",\"rssi\":" + String(WiFi.RSSI(i)) +
                ",\"encryption\":\"" + encryption + "\"}";
    }
    json += "]}";

    xSemaphoreTake(scanResultsMutex, portMAX_DELAY);
    scanResults = json;
    xSemaphoreGive(scanResultsMutex);

    scanComplete = true;
    scanInProgress = false;
    lastScanTime = millis();
    WiFi.scanDelete();
}

void Comms::startAsyncScan()
{
    if (scanInProgress) return;
    scanInProgress = true;
    scanComplete = false;
    scanStartedAt = millis();

    // A successful async start returns WIFI_SCAN_RUNNING (-1); WIFI_SCAN_FAILED
    // (-2) means esp_wifi_scan_start() refused (e.g. the STA is mid-association
    // after an AP-mode rejoin attempt) and NO SCAN_DONE event will ever fire -
    // so the handler below never clears scanInProgress and /scan answers
    // "scanning" for the rest of the session. Reap it here; the scanIsStale()
    // watchdog in scanWiFiNetworks() covers a genuinely lost event.
    if (WiFi.scanNetworks(true) == WIFI_SCAN_FAILED)
    {
        Log.warningln("WiFi scan failed to start - will retry on the next /scan");
        scanInProgress = false;
        return;
    }

    // Registered once rather than on every call: WiFi.onEvent() appends a new callback to the
    // global event list each time, and none of the previous ones are ever removed - a long
    // AP-mode session (the setup page's scan cache misses every 30s) would grow that list
    // without bound and fire every stale copy on each future SCAN_DONE.
    static bool handlerRegistered = false;
    if (!handlerRegistered)
    {
        handlerRegistered = true;
        WiFi.onEvent(
            [](WiFiEvent_t e, WiFiEventInfo_t i)
            {
                if (e != ARDUINO_EVENT_WIFI_SCAN_DONE) return;

                int n = WiFi.scanComplete();
                if (n >= 0) { Comms::Instance().onWiFiScanComplete(n); }
                else
                {
                    // A negative result is scanComplete()'s error code (e.g.
                    // WIFI_SCAN_FAILED) - onWiFiScanComplete() is what clears
                    // scanInProgress, so without this branch a failed scan left it stuck
                    // true forever and /scan returned "scanning" for the rest of the
                    // session, with no way to retry short of a reboot.
                    Log.warningln("WiFi scan failed (code %d)", n);
                    Comms::Instance().scanInProgress = false;
                }
            });
    }
}

String Comms::scanWiFiNetworks()
{
    if (isScanCacheValid(scanComplete, lastScanTime, millis(), SCAN_CACHE_MS))
    {
        xSemaphoreTake(scanResultsMutex, portMAX_DELAY);
        String result = scanResults;
        xSemaphoreGive(scanResultsMutex);
        return result;
    }
    // A scan whose SCAN_DONE event never arrived (see startAsyncScan()) would
    // otherwise keep this method returning "scanning" forever. Reap it so the
    // next line can start a fresh one.
    if (scanIsStale(scanInProgress, scanStartedAt, millis(), SCAN_STALE_MS))
    {
        Log.warningln("WiFi scan stuck for >%lus - abandoning it", SCAN_STALE_MS / 1000);
        scanInProgress = false;
    }
    if (!scanInProgress) startAsyncScan();
    if (scanInProgress) return "{\"networks\":[],\"status\":\"scanning\"}";

    xSemaphoreTake(scanResultsMutex, portMAX_DELAY);
    String result = scanResults;
    xSemaphoreGive(scanResultsMutex);
    return result;
}

// ssid/password copied off the AsyncWebServerRequest before it's freed, handed to
// saveWorkerTask() as its FreeRTOS parameter. Heap-allocated by the /save handler, deleted by
// the worker.
struct SaveJob
{
    String ssid;
    String password;
};

void Comms::saveWorkerTask(void* param)
{
    SaveJob* job = static_cast<SaveJob*>(param);
    Comms& comms = Comms::Instance();

    bool connected = comms.wifiManager->testAndPersistCredentials(job->ssid, job->password);
    String ssid = job->ssid;
    delete job;

    comms.saveStatus = connected ? SaveStatus::Connected : SaveStatus::Failed;

    if (connected)
    {
        // Credentials are persisted and verified. Give the setup client a beat to poll
        // /save-status and see "connected", then reboot - Comms::setup() re-joins with the
        // stored credentials before the web server task exists (blocking there is harmless).
        Log.noticeln("WiFi credential probe succeeded for SSID '%s' - rebooting to connect",
                     ssid.c_str());
        vTaskDelay(3000 / portTICK_PERIOD_MS);
        ESP.restart();
    }
    else
    {
        // testConnection() has already reverted the radio to the setup AP, so the client is
        // still connected and its poll will report "failed". Nothing persisted; stay put for
        // the user to correct the password and resubmit.
        Log.warningln("WiFi credential probe failed for SSID '%s' - staying in AP mode",
                      ssid.c_str());
    }

    vTaskDelete(NULL);
}

bool Comms::processWiFiCredentials(AsyncWebServerRequest* request, const String& ssid,
                                   const String& password)
{
    switch (wifiManager->validateNewCredentials(ssid, password))
    {
        case WifiManager::SaveResult::RejectEmptySsid:
            request->send(400, "text/plain", "SSID required");
            return true;

        case WifiManager::SaveResult::RejectSsidTooLong:
            request->send(400, "text/plain", "SSID too long (32 characters max)");
            return true;

        case WifiManager::SaveResult::RejectPasswordTooLong:
            request->send(400, "text/plain", "Password too long (63 characters max)");
            return true;

        case WifiManager::SaveResult::Accepted:
            // Single-flight. saveWorkerTask holds the one WiFi radio for up to
            // 10 s (WiFi.begin() + a status poll). A second accepted /save
            // while the first probe runs - a typo corrected and re-submitted,
            // or a second phone on the setup AP - would spawn a second worker
            // whose WiFi.begin() supersedes the first's; the first worker's
            // poll then sees WL_CONNECTED for the *second* network, persists
            // its OWN ssid/password, and reboots the device onto credentials
            // that can't join. /save only ever runs on the single async_tcp
            // task, so this check-then-set isn't itself racy.
            if (saveStatus == SaveStatus::Pending)
            {
                request->send(409, "text/plain", "A connection test is already in progress");
                return true;
            }

            // The blocking connection probe can't run here - this handler is on the async_tcp
            // task and a 10s WiFi.status() poll trips its watchdog (#114). Kick it to a worker
            // task and return immediately; the setup client polls GET /save-status for the
            // verdict (pending -> connected | failed). On success the worker reboots into
            // station mode.
            saveStatus = SaveStatus::Pending;
            xTaskCreate(saveWorkerTask, "SaveWiFi", 8192, new SaveJob{ssid, password}, 1, NULL);
            request->send(202, "text/plain", "Testing connection...");
            return true;
    }

    // Every SaveResult value is handled above (kept as a switch, not if/else, precisely so
    // -Wswitch warns if a future value is added and forgotten here) - this is an actual
    // safety net for that case, not routine defensive code: falling through with no response
    // sent leaves the client's request hanging forever instead of erroring cleanly.
    request->send(500, "text/plain", "Internal error");
    return true;
}

size_t Comms::buildCurrentStateJson(char* outBuffer, size_t outBufferSize)
{
    MissionControl& mc = MissionControl::Instance();
    const ModelConfig* runningConfig = GEOMETRY.getConfig();
    ModelId configuredId = FactoryConfig::getModelId();
    const ModelConfig* configuredConfig = getModelConfig(configuredId);
    String configuredDeviceName = DeviceIdentity::getDeviceName();

    // Snapshot under the lock rather than reading runningDeviceName directly - it can be
    // rewritten mid-copy by beginAPBroadcast() on another core (see apStateMux's comment
    // in comms.h).
    char runningNameSnapshot[sizeof(runningDeviceName)];
    portENTER_CRITICAL(&apStateMux);
    strncpy(runningNameSnapshot, runningDeviceName, sizeof(runningNameSnapshot));
    portEXIT_CRITICAL(&apStateMux);

    WsStateBuilder::DeviceState state{
        .power = mc.isOn(),
        .holding = mc.isHolding() || mc.isHoldPending(),
        .brightness = mc.getMaxBrightness(),
        .colorR = mc.staticColor.r,
        .colorG = mc.staticColor.g,
        .colorB = mc.staticColor.b,
        .colorActive = mc.isColorActive(),
        .effectName = mc.getTargetEffectName(),
        .runningModel = {static_cast<uint16_t>(runningConfig->id), runningConfig->name},
        .configuredModel = {static_cast<uint16_t>(configuredId),
                            configuredConfig ? configuredConfig->name : "Unknown"},
        .fps = PerformanceMonitor::Instance().fps(),
        .deviceUid = DeviceIdentity::getUid(),
        .runningDeviceName = runningNameSnapshot,
        .configuredDeviceName = configuredDeviceName.c_str(),
    };
    return WsStateBuilder::buildStateJson(state, outBuffer, outBufferSize);
}

void Comms::broadcastStateIfDirty()
{
    // Throttle to a floor interval, regardless of how fast state is being
    // marked dirty (a live colour/brightness drag does it ~100x/sec). Peek
    // the dirty flag *without* consuming it while inside the window, so the
    // latest state still goes out on the next eligible tick - the final drag
    // value is never lost, just coalesced. Unsigned subtraction is
    // rollover-safe; lastBroadcastMs == 0 means "never broadcast", always due.
    unsigned long now = nowMs();
    if (lastBroadcastMs != 0 && (now - lastBroadcastMs) < MIN_BROADCAST_INTERVAL_MS) return;

    bool dueForKeepalive =
        lastBroadcastMs != 0 && (now - lastBroadcastMs) >= STATE_KEEPALIVE_INTERVAL_MS;

    // consumeStateDirty() always runs (not short-circuited by dueForKeepalive) so a real
    // change is never left pending past this tick just because a keepalive also happened to
    // be due - the flag doesn't need to stay set once we're about to send fresh state anyway.
    bool dirty = MissionControl::Instance().consumeStateDirty();
    if (!dirty && !dueForKeepalive) return;

    char buf[WsStateBuilder::JSON_CAPACITY];
    size_t len = buildCurrentStateJson(buf, sizeof(buf));
    if (len)
    {
        ws.textAll(buf, len);
        lastBroadcastMs = now;
    }
}

void Comms::printWifiStatus()
{
    Log.noticeln("Connected to %s | IP: %s", WiFi.SSID().c_str(),
                 WiFi.localIP().toString().c_str());
}