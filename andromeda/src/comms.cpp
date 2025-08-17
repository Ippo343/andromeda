#ifdef ARDUINO_R4_WIFI

#include "comms.h"

#pragma region Arduino R4 WiFi Implementation

IPAddress local_IP(192, 168, 1, 201);     // Your chosen static IP
IPAddress gateway(192, 168, 1, 1);        // Your router's IP
IPAddress subnet(255, 255, 255, 0);       // Usually 255.255.255.0
IPAddress primaryDNS(8, 8, 8, 8);         // Optional
IPAddress secondaryDNS(8, 8, 4, 4);       // Optional
const byte port = 80;

Comms::Comms(MissionControl& missionControl) :
  server(port),
  mc(missionControl)
{
}

bool Comms::setup()
{
  // This doesn't actually work on an R4 :(
  // That's because the R4 is actually delegating the WiFi to an ESP32,
  // and the WiFi library does not support setting the hostname from the ESP32 core.
  // But who knows, if one day support is added, we are ready.
  WiFi.setHostname("Andromeda");

  // IMPORTANT: Call WiFi.config() *before* WiFi.begin()
  WiFi.config(local_IP, primaryDNS, gateway, subnet);

  status = WiFi.begin(WIFI_SSID, WIFI_PASS);

  // Only do one attempt to connect
  // because everything is synchronous and I don't want it to delay the setup
  // without any way to indicate what's happening
  if (status != WL_CONNECTED)
  {
    Log.errorln("Could not connect to %s, comms disabled", WIFI_SSID);
  }
  else
  {
    server.begin();
    printWifiStatus();
  }

  // Return a success flag so that we can at least show the error animation
  return status == WL_CONNECTED;
}

void Comms::printWifiStatus()
{
  Log.noticeln("Connected to %s with IP %p (%l dBm)", WiFi.SSID(), WiFi.localIP(), WiFi.RSSI());
}

void Comms::loop()
{
  if (status != WL_CONNECTED)
    return;

  WiFiClient client = server.available();   // listen for incoming clients

  if (!client || !client.connected())
    return;

  char requestLine[REQUEST_LINE_BUFFER_SIZE];
  if (readRequestLine(client, requestLine, REQUEST_LINE_BUFFER_SIZE))
  {
    handleRequest(requestLine);
    reply(client);
  }

  delay(1);  // Allow time for client to receive the response
  client.stop();
}

bool Comms::readRequestLine(WiFiClient& client, char* buffer, size_t bufferSize)
{
  size_t index = 0;
  unsigned long start = millis();

  while (client.connected() && millis() - start < REQUEST_TIMEOUT_MS)
  {
    if (client.available())
    {
      char c = client.read();

      if (c == '\n')
      {
        buffer[index] = '\0';
        return true;
      }

      if (c != '\r' && index < bufferSize - 1)
      {
        buffer[index++] = c;
      }
    }
  }

  buffer[0] = '\0';  // fallback empty string
  return false;
}

void Comms::handleRequest(const char* line)
{
  // Expecting something like: "GET /U HTTP/1.1"
  if (strncmp(line, "GET /", 5) != 0)
    return;

  char path = line[5];

  switch (path)
  {
    case 'N':
      mc.handleTransition();
      break;
    case 'H':
      mc.holdEffect();
      break;
    case 'D':
      mc.powerOff();
      break;
    case 'W':
      mc.staticWhite();
      break;
    default:
      // Optional: handle unknown path
      break;
  }
}

void Comms::reply(WiFiClient& client)
{
  milliseconds_t start = millis();

  client.println(
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<body style='background-color:#1a1a1a;'>"
      "<p style='font-size:7vw;'><a href='/N'>Next</a><br></p>"
      "<p style='font-size:7vw;'><a href='/H'>Hold</a><br></p>"
      "<p style='font-size:7vw;'><a href='/D'>Off</a><br></p>"
      "<p style='font-size:7vw;'><a href='/W'>White</a><br></p>"
    "</body>"
  );

  Log.noticeln("WiFi reply took %d milliseconds_t", millis() - start);
}

#pragma endregion

#endif // ARDUINO_R4_WIFI

#ifdef ESP32

#include "comms.h"
#include <ESPAsyncWebServer.h>

#pragma region ESP32 Implementation

const int LED_PIN = 2;  // Built-in LED on most ESP32 boards

Comms::Comms(MissionControl& missionControl) :
  server(80),
  mc(missionControl)
{
}

bool Comms::setup()
{
  // Initialize LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);  // Start with LED off

  // ESP32 supports hostname setting properly
  WiFi.setHostname("Andromeda");

  // Configure static IP for ESP32
  IPAddress local_IP(192, 168, 1, 232);
  IPAddress gateway(192, 168, 1, 1);
  IPAddress subnet(255, 255, 255, 0);

  if (!WiFi.config(local_IP, gateway, subnet)) {
    Log.errorln("Failed to configure static IP");
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  // Wait for connection with 10 second timeout and LED blinking
  unsigned long startTime = millis();
  bool ledState = false;
  unsigned long lastBlink = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
    // Blink LED every 200ms during connection attempt
    if (millis() - lastBlink > 200) {
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

  // Set up routes with clean lambda handlers
  server.on("/N", HTTP_GET, [this](AsyncWebServerRequest *request) {
    unsigned long start = millis();
    mc.queueWebCommand(Command::NEXT);
    sendMainPage(request);
  });

  server.on("/H", HTTP_GET, [this](AsyncWebServerRequest *request) {
    unsigned long start = millis();
    mc.queueWebCommand(Command::HOLD);
    sendMainPage(request);
  });

  server.on("/D", HTTP_GET, [this](AsyncWebServerRequest *request) {
    unsigned long start = millis();
    mc.queueWebCommand(Command::POWER_OFF);
    sendMainPage(request);
  });

  server.on("/W", HTTP_GET, [this](AsyncWebServerRequest *request) {
    unsigned long start = millis();
    mc.queueWebCommand(Command::WHITE);
    sendMainPage(request);
  });

  // Default route (root)
  server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
    unsigned long start = millis();
    sendMainPage(request);
  });

  server.begin();
  printWifiStatus();
  return true;
}

void Comms::sendMainPage(AsyncWebServerRequest *request)
{
  const char* html = R"(
    <body style='background-color:#1a1a1a;'>
      <p style='font-size:7vw;'><a href='/N'>Next</a><br></p>
      <p style='font-size:7vw;'><a href='/H'>Hold</a><br></p>
      <p style='font-size:7vw;'><a href='/D'>Off</a><br></p>
      <p style='font-size:7vw;'><a href='/W'>White</a><br></p>
    </body>
  )";

  request->send(200, "text/html", html);
}

void Comms::printWifiStatus()
{
  Log.noticeln("Connected to %s with IP %s (%d dBm)", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
}

void Comms::loop()
{
  // No loop needed! ESPAsyncWebServer handles everything asynchronously
}

#pragma endregion

#endif // ESP32
