#pragma once

// Trivial no-op stand-in for the ESP32 Arduino core's ESPmDNS.h.

class MDNSClass
{
   public:
    bool begin(const char*) { return true; }
    bool addService(const char*, const char*, uint16_t) { return true; }
    void addServiceTxt(const char*, const char*, const char*, const char*) {}
};

inline MDNSClass MDNS;
