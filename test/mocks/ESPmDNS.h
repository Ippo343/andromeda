#pragma once

// Trivial no-op stand-in for the ESP32 Arduino core's ESPmDNS.h.

class MDNSClass
{
   public:
    bool begin(const char*) { return true; }
};

inline MDNSClass MDNS;
