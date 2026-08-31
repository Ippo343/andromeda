#pragma once

#include <WString.h>

// Pure helpers extracted from Comms, kept free of Arduino/WiFi/networking
// includes so they can be unit tested natively in isolation - comms.cpp
// itself pulls in ESPAsyncWebServer.h/WiFi.h/DNSServer.h/ESPmDNS.h/
// LittleFS.h/Preferences.h, none of which are worth mocking just to reach
// this logic (see the testing plan, Phase 6.5). WString.h itself is fine
// natively - wifi-manager.h already relies on the same String mock.

// Whether a previously completed WiFi scan is still fresh enough to reuse
// instead of starting a new scan.
inline bool isScanCacheValid(bool scanComplete, unsigned long lastScanTime, unsigned long now,
                             unsigned long cacheMs)
{
    return scanComplete && (now - lastScanTime < cacheMs);
}

// Escapes a string for embedding as a JSON string value between the surrounding quotes.
// onWiFiScanComplete() hand-builds JSON out of raw SSIDs; a double-quote or backslash (both
// legal in a WiFi SSID per 802.11) would otherwise produce unparseable JSON that permanently
// jams the WiFi setup page's scan (see WifiSetup's fixed retry budget in wifi.js). Control
// characters are dropped rather than \u-escaped - simpler, and an SSID containing them is
// already unusual.
inline String jsonEscape(const String& s)
{
    String out;
    const char* p = s.c_str();
    for (size_t i = 0; i < s.length(); i++)
    {
        char c = p[i];
        if (c == '"' || c == '\\') out += String('\\');
        if ((unsigned char)c >= 0x20) out += String(c);
    }
    return out;
}
