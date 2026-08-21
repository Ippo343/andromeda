#pragma once

// Pure helpers extracted from Comms, kept free of Arduino/WiFi/networking
// includes so they can be unit tested natively in isolation - comms.cpp
// itself pulls in ESPAsyncWebServer.h/WiFi.h/DNSServer.h/ESPmDNS.h/
// LittleFS.h/Preferences.h, none of which are worth mocking just to reach
// this logic (see the testing plan, Phase 6.5).

// Whether a previously completed WiFi scan is still fresh enough to reuse
// instead of starting a new scan.
inline bool isScanCacheValid(bool scanComplete, unsigned long lastScanTime, unsigned long now,
                             unsigned long cacheMs)
{
    return scanComplete && (now - lastScanTime < cacheMs);
}
