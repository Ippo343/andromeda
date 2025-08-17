#ifdef ARDUINO_R4_WIFI

#include "comms.h"

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

#endif // ARDUINO_R4_WIFI
