#include "comms.h"

#include <DNSServer.h>
#include <esp_system.h>
#include <sys/stat.h>

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
      lastScanTime(0)
{
}

bool Comms::setup()
{
    if (wifiManager->connectUsingStoredCredentials()) { return startStationMode(); }

    Log.noticeln("Starting AP mode for WiFi configuration");
    return startAPMode();
}

bool Comms::startAPMode()
{
    isAPMode = true;
    runningDeviceName = DeviceIdentity::getDeviceName();
    wifiConnector.enterAPMode();

    IPAddress apIP = WiFi.softAPIP();
    Log.noticeln("AP Mode started. IP: %s", apIP.toString().c_str());

    dnsServer = new DNSServer();
    dnsServer->start(DNS_PORT, "*", apIP);

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

bool Comms::startStationMode()
{
    isAPMode = false;
    runningDeviceName = DeviceIdentity::getDeviceName();
    if (!MDNS.begin(DeviceIdentity::getMdnsHostname().c_str())) { Log.errorln("MDNS failed"); }
    createWebServerTask();
    printWifiStatus();
    return true;
}

void Comms::createWebServerTask()
{
    xTaskCreatePinnedToCore(webServerTask, "WebServer", 8192, this, 1, &webServerTaskHandle, 0);
}

void Comms::webServerTask(void* parameter)
{
    Comms* comms = static_cast<Comms*>(parameter);
    comms->setupRoutes();
    comms->server.begin();
    Log.noticeln("Web server started on Core %d", xPortGetCoreID());

    while (true)
    {
        if (comms->isAPMode && comms->dnsServer) { comms->dnsServer->processNextRequest(); }
        comms->broadcastStateIfDirty();
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
                char buf[WsStateBuilder::JSON_CAPACITY];
                size_t stateLen = Comms::Instance().buildCurrentStateJson(buf, sizeof(buf));
                if (stateLen) client->text(buf, stateLen);
            }
            else if (type == WS_EVT_DATA)
            {
                AwsFrameInfo* info = (AwsFrameInfo*)arg;
                if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
                {
                    data[len] = 0;
                    const char* json = reinterpret_cast<const char*>(data);
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
                            mc.setLiveColor(command.r, command.g, command.b);
                        else
                            mc.queueWebCommand(command);
                    }
                    else
                        Log.warningln("Unrecognized WS command message: %s", (char*)data);
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
    STATIC_FILE_ROUTE("/fonts/cinzel.woff2", "font/woff2");
    STATIC_FILE_ROUTE("/js/utils.js", "application/javascript");
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

            char json[320];
            snprintf(json, sizeof(json),
                     "{\"uptimeMs\":%lu,\"heapFree\":%u,\"heapMin\":%u,\"heapTotal\":%u,"
                     "\"tempC\":%s,\"fps\":%s,\"rssi\":%d,\"cpuMhz\":%u,"
                     "\"chip\":\"%s\",\"resetReason\":%d}",
                     static_cast<unsigned long>(millis()), static_cast<unsigned>(ESP.getFreeHeap()),
                     static_cast<unsigned>(ESP.getMinFreeHeap()),
                     static_cast<unsigned>(ESP.getHeapSize()), tempBuf, fpsBuf,
                     static_cast<int>(WiFi.RSSI()), static_cast<unsigned>(ESP.getCpuFreqMHz()),
                     ESP.getChipModel(), static_cast<int>(esp_reset_reason()));
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
    server.on("/save", HTTP_POST, [this](AsyncWebServerRequest* r)
              { processWiFiCredentials(r, r->arg("ssid"), r->arg("password")); });
    server.on("/reset", HTTP_POST,
              [this](AsyncWebServerRequest* r)
              {
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

    // Captive Portal Detection
    server.on("/generate_204", HTTP_GET, [this](AsyncWebServerRequest* r) { r->redirect("/"); });
    server.on("/hotspot-detect.html", HTTP_GET,
              [this](AsyncWebServerRequest* r) { r->redirect("/"); });

    server.onNotFound(
        [this](AsyncWebServerRequest* request)
        {
            if (isAPMode && request->host() != WiFi.softAPIP().toString())
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
        json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    }
    json += "]}";
    scanResults = json;
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
    WiFi.scanNetworks(true);
    WiFi.onEvent(
        [](WiFiEvent_t e, WiFiEventInfo_t i)
        {
            if (e == ARDUINO_EVENT_WIFI_SCAN_DONE)
            {
                int n = WiFi.scanComplete();
                if (n >= 0) Comms::Instance().onWiFiScanComplete(n);
            }
        });
}

String Comms::scanWiFiNetworks()
{
    if (isScanCacheValid(scanComplete, lastScanTime, millis(), SCAN_CACHE_MS)) return scanResults;
    if (!scanInProgress) startAsyncScan();
    return scanInProgress ? "{\"networks\":[],\"status\":\"scanning\"}" : scanResults;
}

bool Comms::processWiFiCredentials(AsyncWebServerRequest* request, const String& ssid,
                                   const String& password)
{
    switch (wifiManager->saveNewCredentials(ssid, password))
    {
        case WifiManager::SaveResult::RejectEmptySsid:
            request->send(400, "text/plain", "SSID required");
            return true;

        case WifiManager::SaveResult::Persisted:
            // The credentials are only saved here, not tested - a blocking
            // WiFi join on this (async_tcp) task trips its watchdog (#114).
            // The device reboots and Comms::setup() attempts the join before
            // the web server starts; on failure it falls back to AP mode and
            // this page is served again.
            request->send(200, "text/html", "<h2>Saved</h2><p>Restarting to connect...</p>");
            xTaskCreate(
                [](void*)
                {
                    vTaskDelay(3000 / portTICK_PERIOD_MS);
                    ESP.restart();
                },
                "Restart", 2048, NULL, 1, NULL);
            return true;
    }
    return true;
}

size_t Comms::buildCurrentStateJson(char* outBuffer, size_t outBufferSize)
{
    MissionControl& mc = MissionControl::Instance();
    const ModelConfig* runningConfig = GEOMETRY.getConfig();
    ModelId configuredId =
        FactoryConfig::isConfigured() ? FactoryConfig::getModelId() : runningConfig->id;
    const ModelConfig* configuredConfig = getModelConfig(configuredId);
    String configuredDeviceName = DeviceIdentity::getDeviceName();

    WsStateBuilder::DeviceState state{
        .power = mc.isOn(),
        .holding = mc.isHolding() || mc.isHoldPending(),
        .brightness = mc.getMaxBrightness(),
        .colorR = mc.staticColor.r,
        .colorG = mc.staticColor.g,
        .colorB = mc.staticColor.b,
        .colorActive = mc.isColorActive(),
        .effectName = mc.getEffectName(),
        .runningModel = {static_cast<uint16_t>(runningConfig->id), runningConfig->name},
        .configuredModel = {static_cast<uint16_t>(configuredId),
                            configuredConfig ? configuredConfig->name : "Unknown"},
        .fps = PerformanceMonitor::Instance().fps(),
        .deviceUid = DeviceIdentity::getUid(),
        .runningDeviceName = runningDeviceName.c_str(),
        .configuredDeviceName = configuredDeviceName.c_str(),
    };
    return WsStateBuilder::buildStateJson(state, outBuffer, outBufferSize);
}

void Comms::broadcastStateIfDirty()
{
    if (!MissionControl::Instance().consumeStateDirty()) return;

    char buf[WsStateBuilder::JSON_CAPACITY];
    size_t len = buildCurrentStateJson(buf, sizeof(buf));
    if (len) ws.textAll(buf, len);
}

void Comms::printWifiStatus()
{
    Log.noticeln("Connected to %s | IP: %s", WiFi.SSID().c_str(),
                 WiFi.localIP().toString().c_str());
}