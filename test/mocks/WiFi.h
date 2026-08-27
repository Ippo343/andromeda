#pragma once

#include <functional>
#include <vector>

#include "WString.h"

// Minimal stand-in for the ESP32 Arduino core's WiFi.h, scoped to what
// comms.cpp itself calls (the real connect/AP-mode polling loops live in
// wifi-esp-adapters.cpp, which stays excluded from the native build - see
// its header comment - so this mock doesn't need to simulate real
// connection timing at all, only compile and behave sanely for the /scan,
// captive-portal, and AP-IP-logging code paths).

using wifi_mode_t = int;
constexpr wifi_mode_t WIFI_STA = 1;
constexpr wifi_mode_t WIFI_AP = 2;

using wl_status_t = int;
constexpr wl_status_t WL_CONNECTED = 3;
constexpr wl_status_t WL_IDLE_STATUS = 0;

using WiFiEvent_t = int;
constexpr WiFiEvent_t ARDUINO_EVENT_WIFI_STA_DISCONNECTED = 1;
constexpr WiFiEvent_t ARDUINO_EVENT_WIFI_STA_GOT_IP = 2;
constexpr WiFiEvent_t ARDUINO_EVENT_WIFI_SCAN_DONE = 3;

struct WiFiEventInfo_t
{
};

class IPAddress
{
   public:
    IPAddress() = default;
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : a(a), b(b), c(c), d(d) {}

    String toString() const
    {
        return String(std::to_string(a) + "." + std::to_string(b) + "." + std::to_string(c) + "." +
                      std::to_string(d));
    }

   private:
    uint8_t a = 0, b = 0, c = 0, d = 0;
};

class WiFiClass
{
   public:
    // Test hooks: script what a scan / status query should report, since
    // nothing here fires a real ARDUINO_EVENT_WIFI_SCAN_DONE callback.
    std::vector<String> scriptedScanSSIDs;
    std::vector<int> scriptedScanRSSIs;
    wl_status_t scriptedStatus = WL_IDLE_STATUS;

    // Test hook: the MAC address macAddress() reports - DeviceIdentity
    // derives its UID from this, so tests can pin it to a known value
    // instead of depending on real hardware.
    uint8_t scriptedMac[6] = {0x24, 0x0A, 0xC4, 0x00, 0x01, 0x02};

    void mode(wifi_mode_t) {}
    void setHostname(const char*) {}
    bool config(IPAddress, IPAddress, IPAddress) { return true; }
    void setAutoReconnect(bool) {}
    void persistent(bool) {}
    void disconnect() {}
    void reconnect() {}
    void softAP(const char*, const char*) {}
    IPAddress softAPIP() const { return IPAddress(192, 168, 4, 1); }
    void begin(const char*, const char*) {}
    wl_status_t status() const { return scriptedStatus; }

    void onEvent(std::function<void(WiFiEvent_t, WiFiEventInfo_t)> cb) { lastEventCallback = cb; }

    void scanNetworks(bool /*async*/) {}
    int scanComplete() const { return static_cast<int>(scriptedScanSSIDs.size()); }
    void scanDelete() {}

    String SSID(int index = -1) const
    {
        if (index < 0 || index >= static_cast<int>(scriptedScanSSIDs.size())) return String("");
        return scriptedScanSSIDs[index];
    }
    int RSSI(int index) const
    {
        if (index < 0 || index >= static_cast<int>(scriptedScanRSSIs.size())) return 0;
        return scriptedScanRSSIs[index];
    }
    // No-arg form: the connected AP's signal strength (WiFi.RSSI() on real hw).
    int scriptedRSSI = -55;
    int RSSI() const { return scriptedRSSI; }

    IPAddress localIP() const { return IPAddress(192, 168, 1, 232); }

    uint8_t* macAddress(uint8_t* mac) const
    {
        for (int i = 0; i < 6; i++) mac[i] = scriptedMac[i];
        return mac;
    }

    std::function<void(WiFiEvent_t, WiFiEventInfo_t)> lastEventCallback;
};

inline WiFiClass WiFi;
