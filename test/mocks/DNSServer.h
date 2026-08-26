#pragma once

#include "WiFi.h"

// Trivial no-op stand-in for the ESP32 Arduino core's DNSServer.h - only
// used by comms.cpp for captive-portal DNS redirection in AP mode, which
// native tests don't exercise (no real network to redirect).

class DNSServer
{
   public:
    void start(int /*port*/, const char* /*domain*/, IPAddress /*ip*/) {}
    void processNextRequest() {}
};
