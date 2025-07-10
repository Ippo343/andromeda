#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <ArduinoLog.h>
#include <string>

#include "WiFiS3.h"
#include "utils.h"
#include "mission-control.h"
#include "secrets.h"

class Comms
{
  private:
    const byte port = 80;
    int status = WL_IDLE_STATUS;
    WiFiServer server;

    MissionControl& mc;

  public:

    Comms(MissionControl& missionControl) :
      server(port),
      mc(missionControl)
    {
    }

    bool setup()
    {
      // This doesn't actually work on an R4 :(
      // That's because the R4 is actually delegating the WiFi to an ESP32,
      // and the WiFi library does not support setting the hostname from the ESP32 core.
      // But who knows, if one day support is added, we are ready.
      WiFi.setHostname("Andromeda");

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

    inline void printWifiStatus()
    {
      Log.noticeln("Connected to %s with IP %p (%l dBm)", WiFi.SSID(), WiFi.localIP(), WiFi.RSSI());
    }

    void loop()
    {
      if (status != WL_CONNECTED)
        return;

      WiFiClient client = server.available();   // listen for incoming clients

      if (!client)
        return;

      // TODO: the code below is taken from the official example, and it sucks.
      // I tried a few times to refactor it, but failed and I cannot figure out why.
      // It will do for the moment, but for the love of God this should be much better.
      // This link has a better tutorial:
      //    https://werner.rothschopf.net/202003_arduino_webserver_post_en.htm
      //
      String currentLine = "";
      while (client.connected())
      {
        if (client.available())
        {
          char c = client.read();
          if (c == '\n')
          {
            // if the current line is blank, you got two newline characters in a row.
            // that's the end of the client HTTP request, so send a response:
            if (currentLine.length() == 0)
            {
              reply(client);
              break;
            }
            else
            {
              currentLine = "";
            }
          }
          else if (c != '\r')
          {
            // if you got anything else but a carriage return character,
            // add it to the end of the currentLine
            currentLine += c;
          }

          if (currentLine.endsWith("GET /N"))
          {
            mc.handleTransition();
          }

          if (currentLine.endsWith("GET /H"))
          {
            mc.holdEffect();
          }

          if (currentLine.endsWith("GET /D"))
          {
            mc.powerOff();
          }

          if (currentLine.endsWith("GET /U"))
          {
            mc.powerOn();
          }
        }
      }

      client.stop();
    }

    void reply(WiFiClient& client)
    {
      milliseconds start = millis();

      // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
      // and a content-type so the client knows what's coming, then a blank line:
      client.println("HTTP/1.1 200 OK");
      client.println("Content-type:text/html");
      client.println();

      // TODO: way more controls (depends on polishing the control logic)
      client.print("<body style=\"background-color:1a1a1a;\">");
      client.print("<p style=\"font-size:7vw;\"><a href=\"/N\">Next</a><br></p>");
      client.print("<p style=\"font-size:7vw;\"><a href=\"/H\">Hold</a><br></p>");
      client.print("<p style=\"font-size:7vw;\"><a href=\"/D\">Off</a><br></p>");
      client.print("<p style=\"font-size:7vw;\"><a href=\"/U\">On</a><br></p>");
      client.print("</body>");

      // End the HTTP response
      client.println();

      Log.noticeln("WiFi reply took %d milliseconds", millis() - start);
    }
};

#endif