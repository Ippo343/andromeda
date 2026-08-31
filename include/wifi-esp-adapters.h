#pragma once

#include "wifi-manager.h"

// Real ESP32 WiFi/Preferences (NVS)-backed implementations of WifiManager's
// interfaces. Deliberately kept in their own translation unit
// (wifi-esp-adapters.cpp), excluded from the native build - unlike the rest
// of comms.cpp, connect() directly polls WiFi.status() in real time (up to a
// 15s timeout) and there's nothing to natively test here beyond what
// WifiManager's own tests already cover against fakes.

class EspWiFiConnector : public IWiFiConnector
{
   public:
    bool connect(const char* ssid, const char* password) override;
    void enterAPMode() override;
};

class EspPreferencesStore : public IPreferencesStore
{
   public:
    void saveCredentials(const String& ssid, const String& password) override;
    bool loadCredentials(String& ssid, String& password) override;
    void clearCredentials() override;
};
