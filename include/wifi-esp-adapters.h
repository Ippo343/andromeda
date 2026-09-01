#pragma once

#include "wifi-manager.h"

// Real ESP32 WiFi/Preferences (NVS)-backed implementations of WifiManager's
// interfaces. Deliberately kept in their own translation unit
// (wifi-esp-adapters.cpp), excluded from the native build - unlike the rest
// of comms.cpp, connect()/testConnection() directly poll WiFi.status() in
// real time (up to a 15s / 10s timeout) and there's nothing to natively test
// here beyond what WifiManager's own tests already cover against fakes.

class EspWiFiConnector : public IWiFiConnector
{
   public:
    bool connect(const char* ssid, const char* password) override;
    bool testConnection(const char* ssid, const char* password) override;
    void enterAPMode() override;
};

class EspPreferencesStore : public IPreferencesStore
{
   public:
    void saveCredentials(const String& ssid, const String& password) override;
    bool loadCredentials(String& ssid, String& password) override;
    void clearCredentials() override;
};

// Starts the background task that periodically retries the stored WiFi credentials while
// the device is sitting in AP mode - whether it got there because the very first boot-time
// join failed, or because a mid-run outage ran past enterAPFallbackMode()'s dead time.
// Without this, either case previously stranded the device on its own open setup AP until
// someone physically power-cycled it, even after the router/network came back.
//
// Idempotent (safe to call every time the device enters AP mode - see
// Comms::beginAPBroadcast()); the task itself starts exactly once per boot and then just
// watches Comms::isInAPMode().
void startApRejoinMonitor();
