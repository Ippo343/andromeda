#include "wifi-esp-adapters.h"

#include <ArduinoLog.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_netif.h>

#include "comms.h"
#include "device-identity.h"
#include "nvs-utils.h"
#include "wifi-recovery.h"

namespace
{
constexpr const char* AP_PASSWORD = "";
constexpr const char* PREFERENCES_NAMESPACE = "wifi";

// Tracks a mid-run disconnect and decides when to retry vs. fall back to AP mode - see
// wifi-recovery.h. Lives for the process lifetime alongside the WiFi event handler that
// drives it; only ever touched from the WiFi event task and the monitor task below, both of
// which are effectively serialized by how rarely they run (an event per disconnect/reconnect,
// a tick once a second), so no additional locking.
WifiRecovery wifiRecovery;

// Polls wifiRecovery roughly once a second and acts on its decision. A dedicated low-priority
// task rather than piggybacking on an existing loop, since neither Comms' web server task nor
// MissionControl's render loop are appropriate owners of WiFi reconnect timing, and this is
// the same "small xTaskCreate for a background concern" shape comms.cpp already uses for its
// setup-page network scan.
void wifiRecoveryMonitorTask(void*)
{
    while (true)
    {
        // Small jitter so multiple devices on the same network don't retry in lockstep.
        WifiRecovery::Action action = wifiRecovery.tick(millis(), random(0, 250));

        switch (action)
        {
            case WifiRecovery::Action::Reconnect:
                Log.warningln("WiFi lost connection, attempting reconnect...");
                WiFi.reconnect();
                break;
            case WifiRecovery::Action::EnterAPMode:
                Log.errorln("WiFi has been unreachable for too long, falling back to AP mode");
                Comms::Instance().enterAPFallbackMode();
                break;
            case WifiRecovery::Action::None:
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
}  // namespace

bool EspWiFiConnector::connect(const char* ssid, const char* password)
{
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(DeviceIdentity::getDeviceName().c_str());

    // DHCP, not a hard-coded static IP: a fixed 192.168.1.x address only worked on networks
    // using that exact subnet - on anything else (192.168.0.x, 10.x, a phone hotspot, ...)
    // the join still "succeeded" but the device was permanently unreachable, and two devices
    // on the same LAN collided on the same address. mDNS (<device-name>.local, wired up in
    // Comms::startStationMode()) is the discovery path now.
    WiFi.persistent(false);

    WiFi.onEvent(
        [](WiFiEvent_t event, WiFiEventInfo_t info)
        {
            switch (event)
            {
                case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
                    wifiRecovery.onDisconnected(millis());
                    break;
                case ARDUINO_EVENT_WIFI_STA_GOT_IP:
                    Log.noticeln("WiFi connected with IP %s", WiFi.localIP().toString().c_str());
                    wifiRecovery.onConnected();
                    break;
                default:
                    break;
            }
        });

    WiFi.begin(ssid, password);

    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 15000) { delay(100); }

    bool connected = WiFi.status() == WL_CONNECTED;

    // Only start the recovery monitor once we've actually joined - if the initial connect
    // failed, Comms::setup() falls back to startAPMode() itself and there's no station
    // connection to recover.
    if (connected)
    {
        xTaskCreate(wifiRecoveryMonitorTask, "WifiRecovery", 2048, nullptr, 1, nullptr);
    }

    return connected;
}

void EspWiFiConnector::enterAPMode()
{
    WiFi.disconnect();
    WiFi.mode(WIFI_AP);
    WiFi.softAP(DeviceIdentity::getDeviceName().c_str(), AP_PASSWORD);

    // The AP-mode DHCP server doesn't offer a DNS-server option by default, so
    // joining phones never learn to query us for DNS and skip captive-portal
    // detection entirely (no auto-popup on iOS/Android). Explicitly offer our
    // own IP as the DNS server so the wildcard DNSServer + hotspot-detect
    // routes in comms.cpp actually get hit.
    esp_netif_t* apNetif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (apNetif)
    {
        esp_netif_dns_info_t dnsInfo;
        dnsInfo.ip.type = ESP_IPADDR_TYPE_V4;
        dnsInfo.ip.u_addr.ip4.addr = static_cast<uint32_t>(WiFi.softAPIP());
        esp_netif_dhcps_stop(apNetif);
        esp_netif_set_dns_info(apNetif, ESP_NETIF_DNS_MAIN, &dnsInfo);
        esp_netif_dhcps_start(apNetif);
    }
    else { Log.warningln("Could not get AP netif handle to set DHCP DNS option"); }
}

void EspPreferencesStore::saveCredentials(const String& ssid, const String& password)
{
    Preferences prefs;
    if (!beginPreferencesOrWarn(prefs, PREFERENCES_NAMESPACE, false)) return;
    putStringOrWarn(prefs, "ssid", ssid);
    // putStringOrWarn already only warns when the write reported 0 bytes for a non-empty
    // value - an empty password (an open network) legitimately writes 0 bytes and is not
    // itself a failure, so this doesn't need a special case.
    putStringOrWarn(prefs, "password", password);
    prefs.end();
}

bool EspPreferencesStore::loadCredentials(String& ssid, String& password)
{
    Preferences prefs;
    if (!beginPreferencesOrWarn(prefs, PREFERENCES_NAMESPACE, true)) return false;
    ssid = prefs.getString("ssid", "");
    password = prefs.getString("password", "");
    prefs.end();
    return ssid.length() > 0;
}

void EspPreferencesStore::clearCredentials()
{
    Preferences prefs;
    if (!beginPreferencesOrWarn(prefs, PREFERENCES_NAMESPACE, false)) return;
    prefs.clear();
    prefs.end();
}
