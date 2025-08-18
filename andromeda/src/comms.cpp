#include "comms.h"

// Built-in LED (used to flash during WiFi connection for my desk tests)
constexpr int LED_PIN = 2;

// Task handle for RTOS to keep the task alive
TaskHandle_t Comms::webServerTaskHandle = nullptr;

Comms::Comms() :
  server(80)
{
}

bool Comms::setup()
{
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

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

  // Disable persistent WiFi credentials.
  // This is an unnecessary optimization: our credentials are hardcoded,
  // so no point storing them in flash with pointless writes.
  // TODO: make the credentials configurable by the user
  WiFi.persistent(false);

  // Attach WiFi event handler for reconnection
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
  milliseconds_t startTime = millis();
  milliseconds_t lastBlink = millis();
  milliseconds_t connectTimeout = 10 SECONDS;
  bool ledState = false;

  while (WiFi.status() != WL_CONNECTED && millis() - startTime < connectTimeout)
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

  // Enable Multicast-DNS: this enables compatible browsers
  // to find the device by visiting "andromeda.local" without needing to know the IP address!
  // ... of course it doesn't work on Windows, why do you ask?
  if (!MDNS.begin("andromeda"))
  {
    Log.errorln("MDNS failed to start");
  }

  // Create web server task on Core 0
  xTaskCreatePinnedToCore(
    webServerTask,          // Function to implement the task
    "WebServer",            // Name of the task
    8192,                   // Stack size in bytes
    this,                   // Task input parameter
    1,                      // Priority (1 is the default, higher numbers indicate higher priority)
    &webServerTaskHandle,   // Task handle
    0                       // Core 0 (low-prio network stack core)
  );

  printWifiStatus();

  return true;
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
    // There is no need to do anything else here,
    // because the ESPAsyncWebServer handles everything in the background
    // using interrupts and callbacks.
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

// Quick helper to reply to a request with a status code and message
inline void replyWithStatus(AsyncWebServerRequest *request, int statusCode, const String &message = "OK")
{
  request->send(statusCode, "text/plain", message);
}

void Comms::setupRoutes()
{
  // Main page
  server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request)
  {
    milliseconds_t start = millis();
    request->send(LittleFS, "/index.html", "text/html");
    Log.noticeln("Main page served in %d ms", millis() - start);
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

  // Fallback for 404 errors
  server.onNotFound([this](AsyncWebServerRequest *request)
  {
    Log.warningln("404 Not Found: %s", request->url().c_str());
    replyWithStatus(request, 404, "Not Found");
  });
}

void Comms::printWifiStatus()
{
  Log.noticeln("Connected to %s with IP %s (%d dBm)", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
}
