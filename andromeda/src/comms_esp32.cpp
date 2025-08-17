#ifdef ESP32

#include "comms.h"
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

const int LED_PIN = 2;  // Built-in LED on most ESP32 boards
TaskHandle_t Comms::webServerTaskHandle = nullptr;

Comms::Comms(MissionControl& missionControl) :
  server(80),
  mc(missionControl)
{
}

bool Comms::setup()
{
  // Initialize LittleFS
  if (!LittleFS.begin())
  {
    Log.errorln("LittleFS Mount Failed");
    return false;
  }
  Log.noticeln("LittleFS mounted successfully");

  // Load static files into RAM
  if (!loadStaticFiles())
  {
    Log.errorln("Failed to load static files");
    return false;
  }

  // Initialize LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);  // Start with LED off

  // ESP32 supports hostname setting properly
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
  WiFi.persistent(false);  // don't store creds in flash

  // Attach WiFi event handler
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info)
  {
    switch (event)
    {
      case SYSTEM_EVENT_STA_DISCONNECTED:
        Log.warningln("WiFi lost connection, attempting reconnect...");
        digitalWrite(LED_PIN, LOW);
        WiFi.reconnect();
        break;
      case SYSTEM_EVENT_STA_GOT_IP:
        Log.noticeln("WiFi reconnected with IP %s", WiFi.localIP().toString().c_str());
        digitalWrite(LED_PIN, HIGH);
        break;
      default:
        break;
    }
  });

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  // Wait for connection with 10 second timeout and LED blinking
  unsigned long startTime = millis();
  bool ledState = false;
  unsigned long lastBlink = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000)
  {
    // Blink LED every 200ms during connection attempt
    if (millis() - lastBlink > 200)
    {
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState ? HIGH : LOW);
      lastBlink = millis();
    }
    delay(50);  // Small delay to prevent busy-waiting
  }
  status = WiFi.status();

  if (status != WL_CONNECTED)
  {
    Log.errorln("Could not connect to %s, comms disabled", WIFI_SSID);
    digitalWrite(LED_PIN, LOW);  // Turn off LED on failure
    return false;
  }

  // Connection successful - keep LED on
  digitalWrite(LED_PIN, HIGH);

  // Create web server task on Core 0
  xTaskCreatePinnedToCore(
    webServerTask,          // Function to implement the task
    "WebServer",            // Name of the task
    8192,                   // Stack size in bytes
    this,                   // Task input parameter
    1,                      // Priority (1 is the default, higher numbers indicate higher priority)
    &webServerTaskHandle,   // Task handle
    0                       // Core 0
  );

  printWifiStatus();
  return true;
}

bool Comms::loadStaticFiles()
{
  bool success = true;

  // Load HTML into RAM
  File file = LittleFS.open("/index.html", "r");
  if (file)
  {
    cachedHTML = file.readString();
    file.close();
    Log.noticeln("Cached HTML (%d bytes)", cachedHTML.length());
  }
  else
  {
    Log.errorln("Failed to load index.html");
    success = false;
  }

  return success;
}

void Comms::webServerTask(void* parameter)
{
  Comms* comms = static_cast<Comms*>(parameter);

  comms->setupRoutes();
  comms->server.begin();
  Log.noticeln("Web server started on Core %d", xPortGetCoreID());

  while (true)
  {
    // Keep the task alive
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

void Comms::setupRoutes()
{
  // Static file routes - served from RAM
  server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request)
  {
    sendMainPage(request);
  });

  // Command routes - redirect back to main page after command
  server.on("/N", HTTP_GET, [this](AsyncWebServerRequest *request)
  {
    mc.queueWebCommand(Command::NEXT);
    sendMainPage(request);
  });

  server.on("/H", HTTP_GET, [this](AsyncWebServerRequest *request)
  {
    mc.queueWebCommand(Command::HOLD);
    sendMainPage(request);
  });

  server.on("/D", HTTP_GET, [this](AsyncWebServerRequest *request)
  {
    mc.queueWebCommand(Command::POWER_OFF);
    sendMainPage(request);
  });

  server.on("/W", HTTP_GET, [this](AsyncWebServerRequest *request)
  {
    mc.queueWebCommand(Command::WHITE);
    sendMainPage(request);
  });

  // Fallback for 404 errors
  server.onNotFound([this](AsyncWebServerRequest *request)
  {
    Log.warningln("404 Not Found: %s", request->url().c_str());
    request->send(404, "text/plain", "Not Found");
  });
}

void Comms::sendMainPage(AsyncWebServerRequest *request)
{
  milliseconds_t start = millis();

  request->send(200, "text/html", cachedHTML);

  Log.noticeln("Main page served in %d ms", millis() - start);
}

void Comms::printWifiStatus()
{
  Log.noticeln("Connected to %s with IP %s (%d dBm)", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
}

void Comms::loop()
{
  // Empty for ESP32 - using async web server
}

#endif // ESP32