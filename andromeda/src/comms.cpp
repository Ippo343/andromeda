#include "comms.h"

#include <DNSServer.h>

#include "ws-command-parser.h"

constexpr int DNS_PORT = 53;

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
    if (!MDNS.begin("andromeda")) { Log.errorln("MDNS failed"); }
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
    STATIC_FILE_ROUTE("/js/controls.js", "application/javascript");
    STATIC_FILE_ROUTE("/js/wifi.js", "application/javascript");
    STATIC_FILE_ROUTE("/css/common.css", "text/css");
    STATIC_FILE_ROUTE("/css/controls.css", "text/css");
    STATIC_FILE_ROUTE("/css/wifi.css", "text/css");

    server.on("/wifi", HTTP_GET,
              [](AsyncWebServerRequest* r) { r->send(LittleFS, "/wifi-setup.html", "text/html"); });

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

        case WifiManager::SaveResult::RejectConnectionFailed:
            request->send(400, "text/plain", "Connection failed");
            return true;

        case WifiManager::SaveResult::Persisted:
            request->send(200, "text/html", "<h2>Success</h2><p>Restarting...</p>");
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

    WsStateBuilder::DeviceState state{
        .power = mc.isOn(),
        .holding = mc.isHolding(),
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