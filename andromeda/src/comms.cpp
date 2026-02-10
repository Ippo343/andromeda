#include "comms.h"

#include <DNSServer.h>
#include <Preferences.h>

// AP mode configuration
constexpr const char* AP_SSID = "Andromeda-Setup";
constexpr const char* AP_PASSWORD = "";
constexpr int DNS_PORT = 53;
constexpr const char* PREFERENCES_NAMESPACE = "wifi";

Comms::Comms()
    : server(80),
      webServerTaskHandle(nullptr),
      dnsServer(nullptr),
      isAPMode(false),
      preferences(),
      scanInProgress(false),
      scanComplete(false),
      scanResults(""),
      lastScanTime(0)
{
}

bool Comms::setup()
{
    // Try to load stored WiFi credentials
    preferences.begin(PREFERENCES_NAMESPACE, true);
    String storedSSID = preferences.getString("ssid", "");
    String storedPassword = preferences.getString("password", "");
    preferences.end();

    if (storedSSID.length() > 0)
    {
        Log.noticeln("Found stored WiFi credentials for: %s", storedSSID.c_str());
        if (connectToWiFi(storedSSID.c_str(), storedPassword.c_str()))
        {
            return startStationMode();
        }
        else
        {
            Log.warningln("Failed to connect to %s with stored credentials", storedSSID.c_str());
        }
    }

    Log.noticeln("Starting AP mode for WiFi configuration");
    return startAPMode();
}

bool Comms::connectToWiFi(const char* ssid, const char* password)
{
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("Andromeda");

    // Configure static IP
    // TODO: this should be configurable
    IPAddress local_IP(192, 168, 1, 232);
    IPAddress gateway(192, 168, 1, 1);
    IPAddress subnet(255, 255, 255, 0);
    if (!WiFi.config(local_IP, gateway, subnet)) { Log.errorln("Failed to configure static IP"); }

    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);

    WiFi.onEvent(
        [](WiFiEvent_t event, WiFiEventInfo_t info)
        {
            switch (event)
            {
                case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
                    Log.warningln("WiFi lost connection, attempting reconnect...");
                    WiFi.reconnect();
                    break;
                case ARDUINO_EVENT_WIFI_STA_GOT_IP:
                    Log.noticeln("WiFi reconnected with IP %s", WiFi.localIP().toString().c_str());
                    break;
                default:
                    break;
            }
        });

    WiFi.begin(ssid, password);

    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 15000) { delay(100); }

    status = WiFi.status();
    return status == WL_CONNECTED;
}

bool Comms::startAPMode()
{
    isAPMode = true;
    WiFi.disconnect();
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);

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
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void Comms::setupRoutes()
{
    // Global static files
    server.on("/", HTTP_GET,
              [](AsyncWebServerRequest* r) { r->send(LittleFS, "/index.html", "text/html"); });
    server.on("/common.css", HTTP_GET,
              [](AsyncWebServerRequest* r) { r->send(LittleFS, "/common.css", "text/css"); });
    server.on("/setup", HTTP_GET,
              [](AsyncWebServerRequest* r) { r->send(LittleFS, "/wifi-setup.html", "text/html"); });

    // Shared Command routes
    server.on("/N", HTTP_POST,
              [](AsyncWebServerRequest* r)
              {
                  MissionControl::Instance().queueWebCommand(Command::NEXT);
                  r->send(200);
              });
    server.on("/H", HTTP_POST,
              [](AsyncWebServerRequest* r)
              {
                  MissionControl::Instance().queueWebCommand(Command::HOLD);
                  r->send(200);
              });
    server.on("/D", HTTP_POST,
              [](AsyncWebServerRequest* r)
              {
                  MissionControl::Instance().queueWebCommand(Command::POWER_OFF);
                  r->send(200);
              });

    server.on("/color", HTTP_POST,
              [](AsyncWebServerRequest* r)
              {
                  Log.noticeln("Received color command with args: %s", r->args() > 0 ? "" : "none");
                  if (r->hasArg("r") && r->hasArg("g") && r->hasArg("b"))
                  {
                      int red = r->arg("r").toInt();
                      int green = r->arg("g").toInt();
                      int blue = r->arg("b").toInt();

                      if (red >= 0 && red <= 255 && green >= 0 && green <= 255 && blue >= 0 &&
                          blue <= 255)
                      {
                          MissionControl::Instance().staticColor = CRGB(red, green, blue);
                          MissionControl::Instance().queueWebCommand(Command::COLOR);
                          r->send(200);
                      }
                      else { r->send(400, "text/plain", "RGB values must be 0-255"); }
                  }
                  else { r->send(400, "text/plain", "Missing r, g, or b parameter"); }
              });

    // Shared Config & Monitoring
    server.on("/fps", HTTP_GET, [](AsyncWebServerRequest* r)
              { r->send(200, "text/plain", String(PerformanceMonitor::Instance().fps())); });
    server.on(
        "/brightness", HTTP_GET, [](AsyncWebServerRequest* r)
        { r->send(200, "text/plain", String(MissionControl::Instance().getMaxBrightness())); });
    server.on("/brightness", HTTP_POST,
              [](AsyncWebServerRequest* r)
              {
                  int val = r->arg("value").toInt();
                  if (val >= 0 && val <= 255)
                  {
                      MissionControl::Instance().setMaxBrightness(val);
                      r->send(200);
                  }
                  else { r->send(400); }
              });

    // WiFi functionality
    server.on("/scan", HTTP_GET, [this](AsyncWebServerRequest* r)
              { r->send(200, "application/json", scanWiFiNetworks()); });
    server.on("/save", HTTP_POST, [this](AsyncWebServerRequest* r)
              { processWiFiCredentials(r, r->arg("ssid"), r->arg("password")); });
    server.on("/reset", HTTP_POST,
              [this](AsyncWebServerRequest* r)
              {
                  preferences.begin(PREFERENCES_NAMESPACE, false);
                  preferences.clear();
                  preferences.end();
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
                request->redirect("http://" + WiFi.softAPIP().toString() + "/setup");
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
    if (scanComplete && (millis() - lastScanTime < SCAN_CACHE_MS)) return scanResults;
    if (!scanInProgress) startAsyncScan();
    return scanInProgress ? "{\"networks\":[],\"status\":\"scanning\"}" : scanResults;
}

bool Comms::testWiFiConnection(const char* ssid, const char* password)
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40)
    {
        delay(250);
        attempts++;
    }
    bool ok = (WiFi.status() == WL_CONNECTED);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    return ok;
}

bool Comms::processWiFiCredentials(AsyncWebServerRequest* request, const String& ssid,
                                   const String& password)
{
    if (ssid.length() == 0)
    {
        request->send(400, "text/plain", "SSID required");
        return true;
    }
    if (testWiFiConnection(ssid.c_str(), password.c_str()))
    {
        preferences.begin(PREFERENCES_NAMESPACE, false);
        preferences.putString("ssid", ssid);
        preferences.putString("password", password);
        preferences.end();
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
    request->send(400, "text/plain", "Connection failed");
    return true;
}

void Comms::printWifiStatus()
{
    Log.noticeln("Connected to %s | IP: %s", WiFi.SSID().c_str(),
                 WiFi.localIP().toString().c_str());
}
