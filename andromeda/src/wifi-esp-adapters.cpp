#include "wifi-esp-adapters.h"

#include <ArduinoLog.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_netif.h>

#include "device-identity.h"

namespace
{
constexpr const char* AP_PASSWORD = "";
constexpr const char* PREFERENCES_NAMESPACE = "wifi";
}  // namespace

bool EspWiFiConnector::connect(const char* ssid, const char* password)
{
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(DeviceIdentity::getDeviceName().c_str());

    // Configure static IP
    // TODO: this should be configurable
    IPAddress local_IP(192, 168, 1, 232);
    IPAddress gateway(192, 168, 1, 1);
    IPAddress subnet(255, 255, 255, 0);
    if (!WiFi.config(local_IP, gateway, subnet)) { Log.errorln("Failed to configure static IP"); }

    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);

    WiFi.onEvent(
        [](WiFiEvent_t event, WiFiEventInfo_t info)
        {
            switch (event)
            {
                case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
                    Log.warningln("WiFi lost connection, attempting reconnect...");
                    WiFi.reconnect();
                    break;
                case ARDUINO_EVENT_WIFI_STA_GOT_IP:
                    Log.noticeln("WiFi reconnected with IP %s", WiFi.localIP().toString().c_str());
                    break;
                default:
                    break;
            }
        });

    WiFi.begin(ssid, password);

    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 15000) { delay(100); }

    return WiFi.status() == WL_CONNECTED;
}

bool EspWiFiConnector::testConnection(const char* ssid, const char* password)
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40)
    {
        delay(250);
        attempts++;
    }
    bool ok = (WiFi.status() == WL_CONNECTED);
    enterAPMode();
    return ok;
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
    prefs.begin(PREFERENCES_NAMESPACE, false);
    prefs.putString("ssid", ssid);
    prefs.putString("password", password);
    prefs.end();
}

bool EspPreferencesStore::loadCredentials(String& ssid, String& password)
{
    Preferences prefs;
    prefs.begin(PREFERENCES_NAMESPACE, true);
    ssid = prefs.getString("ssid", "");
    password = prefs.getString("password", "");
    prefs.end();
    return ssid.length() > 0;
}

void EspPreferencesStore::clearCredentials()
{
    Preferences prefs;
    prefs.begin(PREFERENCES_NAMESPACE, false);
    prefs.clear();
    prefs.end();
}
