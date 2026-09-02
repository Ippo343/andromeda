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

// (Re)apply the SoftAP + the DHCP DNS-server option. Both EspWiFiConnector::
// enterAPMode() and apRejoinMonitorTask()'s failed-rejoin path go through
// here so they can't drift: the rejoin path used to do a bare
// `WiFi.mode(WIFI_AP)` after a WIFI_AP_STA probe, which on some cores drops
// the softAP config and always drops the DNS option below - so after the
// *first* failed rejoin the captive-portal popup stopped appearing and the
// setup page was only reachable by typing the IP.
void reapplyApConfig()
{
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

// Tracks a mid-run disconnect and decides when to retry vs. fall back to AP mode - see
// wifi-recovery.h. Lives for the process lifetime. The WiFi event task only ever
// postEvent()s into it; the monitor task below is the only one that tick()s (and so the only
// one that mutates the state machine), so there's no shared read-modify-write to lock.
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

// How often to retry the stored credentials while parked in AP mode, and how long each
// probe gets. Deliberately not as fast as wifiRecoveryMonitorTask's 1s poll - this
// probe, unlike that one, briefly interrupts the setup AP for anyone connected to it (see
// below), so it shouldn't be so eager that provisioning a device is ever visibly disrupted
// by an unrelated retry.
constexpr uint32_t AP_REJOIN_RETRY_INTERVAL_MS = 60000;
constexpr uint32_t AP_REJOIN_PROBE_TIMEOUT_MS = 8000;

// Runs for the process lifetime once started (see startApRejoinMonitor()). Periodically
// retries whatever credentials are currently stored, so a router coming back - or the
// user's home network simply taking a while to boot alongside this device after a power
// outage - lets the device silently rejoin instead of requiring a manual reconnect via
// the setup AP.
//
// Uses WIFI_AP_STA (not testConnection()'s WIFI_STA-only probe) specifically so the setup
// AP keeps serving throughout: testConnection() is a deliberate, user-initiated /save
// probe where a brief AP drop is expected and shown in the UI; this one runs unattended
// in the background and must not disrupt someone who happens to be using the setup page
// at the same moment.
void apRejoinMonitorTask(void*)
{
    EspPreferencesStore store;

    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(AP_REJOIN_RETRY_INTERVAL_MS + random(0, 2000)));

        if (!Comms::Instance().isInAPMode()) continue;

        String ssid, password;
        if (!store.loadCredentials(ssid, password))
            continue;  // never configured - nothing to retry

        WiFi.mode(WIFI_AP_STA);
        WiFi.begin(ssid.c_str(), password.c_str());

        unsigned long startTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startTime < AP_REJOIN_PROBE_TIMEOUT_MS)
        {
            delay(100);
        }

        if (WiFi.status() == WL_CONNECTED)
        {
            // Simplest correct way to actually commit to station mode: reboot, exactly
            // like Comms::saveWorkerTask does on a successful /save probe.
            // Comms::setup() -> connectUsingStoredCredentials() re-joins with these same
            // (already-persisted) credentials on the way back up.
            Log.noticeln("Rejoined stored WiFi network from AP mode - rebooting to connect");
            delay(500);
            ESP.restart();
        }
        else
        {
            // Revert cleanly to AP-only. A failed STA join inside WIFI_AP_STA can leave
            // the radio in a half-joined state on some cores if left as-is - and a bare
            // WiFi.mode(WIFI_AP) here dropped the softAP + captive-portal DNS option, so
            // go through the same reapplyApConfig() enterAPMode() uses.
            WiFi.disconnect();
            reapplyApConfig();
        }
    }
}
}  // namespace

void startApRejoinMonitor()
{
    static bool started = false;
    if (started) return;
    started = true;
    xTaskCreate(apRejoinMonitorTask, "APRejoin", 3072, nullptr, 1, nullptr);
}

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
                    wifiRecovery.postEvent(WifiRecovery::Event::Disconnected, millis());
                    break;
                case ARDUINO_EVENT_WIFI_STA_GOT_IP:
                    Log.noticeln("WiFi connected with IP %s", WiFi.localIP().toString().c_str());
                    wifiRecovery.postEvent(WifiRecovery::Event::Connected, millis());
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

bool EspWiFiConnector::testConnection(const char* ssid, const char* password)
{
    // A candidate-credential probe, driven from Comms::saveWorkerTask (never
    // async_tcp, #114). Deliberately shorter than connect()'s 15s boot budget:
    // the setup client is holding the page waiting on /save-status, so favour
    // a quick verdict. Always reverts to the setup AP afterwards - on failure
    // so the user can retry, on success so the client can still read the
    // "connected" result before the device reboots into station mode.
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) { delay(100); }

    bool ok = (WiFi.status() == WL_CONNECTED);
    enterAPMode();
    return ok;
}

void EspWiFiConnector::enterAPMode()
{
    WiFi.disconnect();
    reapplyApConfig();
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
