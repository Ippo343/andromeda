#pragma once

#include <WString.h>

#include <cstring>

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

// Whether an HTTP/WebSocket request's Origin names the same authority as its
// Host. `origin` is the raw Origin header value ("scheme://host[:port]");
// `host` is request->host() ("host[:port]"). The scheme is stripped and the
// remainder must match `host` exactly.
//
// The old check was `origin.indexOf(host) != -1` - substring containment,
// which a hostile hostname defeats: with Host "andromeda-ab12.local", an
// Origin of "http://andromeda-ab12.local.evil.example" contains the host as a
// substring and was waved through, and a port mismatch
// ("http://192.168.1.50:8080" vs Host "192.168.1.50") was likewise treated as
// same-origin. An empty/absent origin returns false here; callers decide
// whether "no Origin at all" (curl, a native app, a same-origin fetch) is
// allowed.
inline bool originMatchesHost(const char* origin, const char* host)
{
    if (!origin || !*origin || !host || !*host) return false;

    const char* sep = strstr(origin, "://");
    const char* authority = sep ? sep + 3 : origin;

    // An Origin has no path, but be defensive about a trailing '/'.
    const char* end = strchr(authority, '/');
    size_t authorityLen = end ? static_cast<size_t>(end - authority) : strlen(authority);

    return authorityLen == strlen(host) && strncmp(authority, host, authorityLen) == 0;
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
