#include "comms.h"
#include <Preferences.h>
#include <DNSServer.h>

// AP mode configuration
constexpr const char* AP_SSID = "Andromeda-Setup";
constexpr const char* AP_PASSWORD = ""; // Open network for easier setup
constexpr int DNS_PORT = 53;

Comms::Comms() :
  server(80),
  webServerTaskHandle(nullptr),
  dnsServer(nullptr),
  isAPMode(false),
  preferences()
{
}

bool Comms::setup()
{
  // Try to load stored WiFi credentials
  preferences.begin("wifi", false);
  String storedSSID = preferences.getString("ssid", "");
  String storedPassword = preferences.getString("password", "");

  bool connectionSuccess = false;

  // Stored credentials, try to reconnect
  if (storedSSID.length() > 0)
  {
    Log.noticeln("Found stored WiFi credentials for: %s", storedSSID.c_str());
    connectionSuccess = connectToWiFi(storedSSID.c_str(), storedPassword.c_str());
  }
  else
  {
    Log.noticeln("No stored WiFi credentials found");
  }

  // Either no credentials found or connection failed:
  // start the AP for configuration
  if (!connectionSuccess)
  {
    Log.noticeln("Starting AP mode for WiFi configuration");
    return startAPMode();
  }

  return startStationMode();
}

bool Comms::connectToWiFi(const char* ssid, const char* password)
{
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("Andromeda");

  // Configure static IP for ESP32
  IPAddress local_IP(192, 168, 1, 232);
  IPAddress gateway(192, 168, 1, 1);
  IPAddress subnet(255, 255, 255, 0);
  if (!WiFi.config(local_IP, gateway, subnet))
  {
    Log.errorln("Failed to configure static IP");
  }

  // Enable auto reconnect
  WiFi.setAutoReconnect(true);

  // Disable WiFi persistence:
  // that's because we are manually persisting the credentials
  // using Preferences, and this is a less-controllable duplicate.
  WiFi.persistent(false);

  // Attach WiFi event handler for reconnection
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info)
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

  // Wait for connection with 15 second timeout
  unsigned long startTime = millis();
  unsigned long connectTimeout = 15000; // 15 seconds

  while (WiFi.status() != WL_CONNECTED && millis() - startTime < connectTimeout)
  {
    delay(100);  // Check every 100ms
  }

  status = WiFi.status();

  if (status != WL_CONNECTED)
  {
    Log.errorln("Could not connect to %s", ssid);
    return false;
  }

  return true;
}

bool Comms::startAPMode()
{
  isAPMode = true;

  // Stop any existing WiFi connection
  WiFi.disconnect();

  // Start Access Point
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  // Get the AP IP address
  IPAddress apIP = WiFi.softAPIP();
  Log.noticeln("AP Mode started. SSID: %s, IP: %s", AP_SSID, apIP.toString().c_str());

  // Start DNS server for captive portal
  dnsServer = new DNSServer();
  dnsServer->start(DNS_PORT, "*", apIP); // Redirect all DNS queries to our IP

  createWebServerTask();

  return true;
}

bool Comms::startStationMode()
{
  isAPMode = false;

  // Enable Multicast-DNS
  if (!MDNS.begin("andromeda"))
  {
    Log.errorln("MDNS failed to start");
  }

  createWebServerTask();
  printWifiStatus();

  return true;
}

inline void Comms::createWebServerTask()
{
  xTaskCreatePinnedToCore(
    webServerTask,          // Function to implement the task
    "WebServer",            // Name of the task
    8192,                   // Stack size in bytes
    this,                   // Task input parameter
    1,                      // Priority
    &webServerTaskHandle,   // Task handle
    0                       // Core 0
  );
}

// This is the code for the RTOS task that keeps the server alive
void Comms::webServerTask(void* parameter)
{
  Comms* comms = static_cast<Comms*>(parameter);

  comms->setupRoutes();
  comms->server.begin();
  Log.noticeln("Web server started on Core %d", xPortGetCoreID());

  while (true)
  {
    // Handle DNS requests for captive portal in AP mode
    if (comms->isAPMode && comms->dnsServer) {
      comms->dnsServer->processNextRequest();
    }

    vTaskDelay(10 / portTICK_PERIOD_MS); // Reduced delay for DNS processing
  }
}

// Quick helper to reply to a request with a status code and message
inline void replyWithStatus(AsyncWebServerRequest *request, int statusCode, const String &message = "OK")
{
  request->send(statusCode, "text/plain", message);
}

void Comms::setupRoutes()
{
  server.on("/common.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/common.css", "text/css");
  });

  if (isAPMode) {
    setupAPRoutes();
  } else {
    setupStationRoutes();
  }
}

void Comms::setupAPRoutes()
{
  // Captive portal - redirect everything to setup page
  server.onNotFound([this](AsyncWebServerRequest *request) {
    // Check if this is a captive portal detection request
    String host = request->host();
    if (host != WiFi.softAPIP().toString()) {
      // Redirect to our setup page
      request->redirect("http://" + WiFi.softAPIP().toString() + "/setup");
      return;
    }

    // Serve setup page for any request
    serveSetupPage(request);
  });

  // Setup page
  server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
    serveSetupPage(request);
  });

  server.on("/setup", HTTP_GET, [this](AsyncWebServerRequest *request) {
    serveSetupPage(request);
  });

  // WiFi scan endpoint
  server.on("/scan", HTTP_GET, [this](AsyncWebServerRequest *request) {
    String json = scanWiFiNetworks();
    request->send(200, "application/json", json);
  });

  // Save WiFi credentials (form POST)
  server.on("/save", HTTP_POST, [this](AsyncWebServerRequest *request) {
    Log.noticeln("Received POST to /save with %d params", request->params());

    Log.noticeln("Headers:");
    int headers = request->headers();
    for (int i = 0; i < headers; i++) {
        AsyncWebHeader* h = request->getHeader(i);
        Log.noticeln("  %s: %s", h->name().c_str(), h->value().c_str());
    }

    if (request->hasParam("plain", true)) {
        String body = request->getParam("plain", true)->value();
        Log.noticeln("Raw body (plain): %s", body.c_str());
    }

    // Extract SSID and password from form args
    String ssid = request->arg("ssid");
    String password = request->arg("password");

    ssid.trim();
    password.trim();

    Log.noticeln("Parsed from form - SSID: '%s', Password length: %d",
                  ssid.c_str(), password.length());

    if (ssid.length() == 0) {
        request->send(400, "text/html",
            "<html><body style='font-family:Arial;text-align:center;margin-top:50px;'>"
            "<h2>Missing Information</h2>"
            "<p>Please provide a network name (SSID) and try again.</p>"
            "<a href='/setup'>Go Back</a>"
            "</body></html>");
        return;
    }

    if (processWiFiCredentials(request, ssid, password)) {
        return; // processed successfully (either saved + restart or failed to connect)
    }

    // fallback in case processWiFiCredentials did not handle the request
    request->send(500, "text/html",
        "<html><body style='font-family:Arial;text-align:center;margin-top:50px;'>"
        "<h2>Internal Error</h2>"
        "<p>Could not process the request.</p>"
        "<a href='/setup'>Go Back</a>"
        "</body></html>");
  });

    // Reset credentials (for debugging/recovery)
    server.on("/reset", HTTP_POST, [this](AsyncWebServerRequest *request) {
      preferences.clear();
      request->send(200, "text/html",
        "<html><body style='font-family:Arial;text-align:center;margin-top:50px;'>"
        "<h2>Credentials Cleared</h2>"
        "<p>Device will restart in AP mode.</p>"
        "</body></html>");

      xTaskCreatePinnedToCore([](void*){
          vTaskDelay(3000 / portTICK_PERIOD_MS);
          ESP.restart();
      }, "RestartTask", 2048, nullptr, 1, nullptr, 0);
    });
}

void Comms::setupStationRoutes()
{
  // Main page
  server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request)
  {
    unsigned long start = millis();
    request->send(LittleFS, "/index.html", "text/html");
    Log.noticeln("Main page served in %lu ms", millis() - start);
  });

  // Command routes (POST only, return minimal response)
  server.on("/N", HTTP_POST, [this](AsyncWebServerRequest *request)
  {
    MissionControl::Instance().queueWebCommand(Command::NEXT);
    replyWithStatus(request, 200);
  });

  server.on("/H", HTTP_POST, [this](AsyncWebServerRequest *request)
  {
    MissionControl::Instance().queueWebCommand(Command::HOLD);
    replyWithStatus(request, 200);
  });

  server.on("/D", HTTP_POST, [this](AsyncWebServerRequest *request)
  {
    MissionControl::Instance().queueWebCommand(Command::POWER_OFF);
    replyWithStatus(request, 200);
  });

  server.on("/W", HTTP_POST, [this](AsyncWebServerRequest *request)
  {
    MissionControl::Instance().queueWebCommand(Command::WHITE);
    replyWithStatus(request, 200);
  });

  server.on("/fps", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", String(PerformanceMonitor::Instance().fps()));
  });

  // WiFi reset endpoint (for reconfiguration)
  server.on("/wifi-reset", HTTP_GET, [this](AsyncWebServerRequest *request) {
    preferences.clear();
    request->send(200, "text/plain", "WiFi credentials cleared. Device will restart in AP mode.");

    // Restart on a separate task after 3s
    xTaskCreatePinnedToCore([](void*){
        vTaskDelay(3000 / portTICK_PERIOD_MS);
        ESP.restart();
    }, "RestartTask", 2048, nullptr, 1, nullptr, 0);
  });

  server.on("/logs", HTTP_GET, [this](AsyncWebServerRequest *request) {
    request->send(LittleFS, LOG_FILE_OLD, "text/plain");
    request->send(LittleFS, LOG_FILE_CUR, "text/plain");
  });

  server.on("/brightness", HTTP_GET, [this](AsyncWebServerRequest *request){
    Log.noticeln("GET brightness: %d", MissionControl::Instance().getMaxBrightness());
    request->send(200, "text/plain", String(MissionControl::Instance().getMaxBrightness()));
  });

  server.on("/brightness", HTTP_POST, [this](AsyncWebServerRequest *request){
    int brightness = request->arg("value").toInt();
    if (brightness >= 0 && brightness <= 255) {
      MissionControl::Instance().setMaxBrightness(brightness);
      Log.noticeln("POST brightness: %d", brightness);
      request->send(200, "text/plain", "OK");
    } else {
      request->send(400, "text/plain", "Invalid range");
    }
  });

  // Fallback for 404 errors
  server.onNotFound([this](AsyncWebServerRequest *request)
  {
    Log.warningln("404 Not Found: %s", request->url().c_str());
    replyWithStatus(request, 404, "Not Found");
  });
}

void Comms::serveSetupPage(AsyncWebServerRequest *request)
{
  request->send(LittleFS, "/wifi-setup.html", "text/html");
}

String Comms::scanWiFiNetworks()
{
  int networkCount = WiFi.scanNetworks();

  String json = "{\"networks\":[";

  for (int i = 0; i < networkCount; i++) {
    if (i > 0) json += ",";

    String encryption = "none";
    switch (WiFi.encryptionType(i)) {
      case WIFI_AUTH_WEP: encryption = "WEP"; break;
      case WIFI_AUTH_WPA_PSK: encryption = "WPA"; break;
      case WIFI_AUTH_WPA2_PSK: encryption = "WPA2"; break;
      case WIFI_AUTH_WPA_WPA2_PSK: encryption = "WPA/WPA2"; break;
      case WIFI_AUTH_WPA2_ENTERPRISE: encryption = "WPA2-Enterprise"; break;
      default: encryption = "none"; break;
    }

    json += "{";
    json += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"encryption\":\"" + encryption + "\"";
    json += "}";
  }

  json += "]}";

  WiFi.scanDelete(); // Clean up
  return json;
}

String Comms::urlDecode(String str) {
  String decoded = "";
  char temp[] = "0x00";
  unsigned int len = str.length();
  unsigned int i = 0;

  while (i < len) {
    char decodedChar;
    char encodedChar = str.charAt(i++);

    if ((encodedChar == '%') && (i + 1 < len)) {
      temp[2] = str.charAt(i++);
      temp[3] = str.charAt(i++);
      decodedChar = strtol(temp, NULL, 16);
    } else if (encodedChar == '+') {
      decodedChar = ' ';
    } else {
      decodedChar = encodedChar;
    }

    decoded += decodedChar;
  }

  return decoded;
}

bool Comms::testWiFiConnection(const char* ssid, const char* password)
{
  // Temporarily switch to station mode to test connection
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  // Wait up to 10 seconds for connection
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(250);
    attempts++;
  }

  bool connected = (WiFi.status() == WL_CONNECTED);

  if (connected) {
    Log.noticeln("Test connection successful to %s", ssid);
    WiFi.disconnect(); // Disconnect the test connection
  } else {
    Log.errorln("Test connection failed to %s", ssid);
  }

  // Switch back to AP mode
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  return connected;
}

void Comms::printWifiStatus()
{
  Log.noticeln("Connected to %s with IP %s (%d dBm)", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
}

bool Comms::processWiFiCredentials(AsyncWebServerRequest* request, const String& ssid, const String& password)
{
  if (ssid.length() > 0) {
    Log.noticeln("Attempting to connect to: %s", ssid.c_str());

    // Test the connection
    if (testWiFiConnection(ssid.c_str(), password.c_str())) {
      // Save credentials
      preferences.putString("ssid", ssid);
      preferences.putString("password", password);

      request->send(200, "text/html",
        "<html><body style='font-family:Arial;text-align:center;margin-top:50px;'>"
        "<h2>WiFi Credentials Saved!</h2>"
        "<p>Device will restart and connect to your network.</p>"
        "<p>You can now access it at <strong>andromeda.local</strong> or <strong>192.168.1.232</strong></p>"
        "</body></html>");

        // Restart on a separate task after 3s
        // Note: you need a separate task here because this code is running
        // inside the web request handler. If we delay() here we block the request,
        // so we delay and restart without ever completing the request and the browser
        // never receives the success response.
        xTaskCreatePinnedToCore([](void*){
            vTaskDelay(3000 / portTICK_PERIOD_MS);
            ESP.restart();
        }, "RestartTask", 2048, nullptr, 1, nullptr, 0);

        return true;
    }
    else {
      request->send(400, "text/html",
        "<html><body style='font-family:Arial;text-align:center;margin-top:50px;'>"
        "<h2>Connection Failed</h2>"
        "<p>Could not connect to the specified network. Please check your credentials and try again.</p>"
        "<a href='/setup'>Go Back</a>"
        "</body></html>");
      return true;
    }
  } else {
    Log.errorln("SSID is empty");
    request->send(400, "text/html",
      "<html><body style='font-family:Arial;text-align:center;margin-top:50px;'>"
      "<h2>Missing Information</h2>"
      "<p>Please provide a network name (SSID) and try again.</p>"
      "<a href='/setup'>Go Back</a>"
      "</body></html>");
    return true;
  }
}