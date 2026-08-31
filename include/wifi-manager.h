#pragma once

#include <WString.h>

// Orchestration logic for WiFi credential save/load, extracted out of Comms
// so it can be exercised natively without WiFi/Preferences hardware. All
// blocking network I/O and NVS access lives behind these two interfaces;
// WifiManager itself never calls delay()/millis() or touches WiFi/Preferences
// directly, so a test-double IWiFiConnector can return a scripted result
// instantly instead of polling in real time.
//
// Newly-submitted credentials are persisted immediately and verified on the
// next boot (Comms::setup() -> connectUsingStoredCredentials(), which runs
// before the web server task exists). saveNewCredentials() deliberately does
// NOT connect: it is invoked from the /save HTTP handler on the async_tcp
// task, and a 10-15s blocking WiFi.status() poll there starves that task past
// its watchdog and panics the device (#114).

class IWiFiConnector
{
   public:
    virtual ~IWiFiConnector() = default;

    // Blocking, used for previously-stored credentials at boot. Commits to
    // station mode on success (configures a static IP, sets up
    // auto-reconnect). Real implementations poll WiFi.status() with a
    // delay()/millis()-based timeout internally; this call either returns
    // once connected or once it's given up. Only ever called from
    // Comms::setup(), before the web server / async_tcp task is started, so
    // blocking here is harmless.
    virtual bool connect(const char* ssid, const char* password) = 0;

    // Falls back to broadcasting the setup access point.
    virtual void enterAPMode() = 0;
};

class IPreferencesStore
{
   public:
    virtual ~IPreferencesStore() = default;

    virtual void saveCredentials(const String& ssid, const String& password) = 0;
    // Returns false (and leaves ssid/password untouched) if nothing is stored.
    virtual bool loadCredentials(String& ssid, String& password) = 0;
    virtual void clearCredentials() = 0;
};

class WifiManager
{
   public:
    WifiManager(IWiFiConnector& connector, IPreferencesStore& store)
        : connector(connector), store(store)
    {
    }

    enum class SaveResult
    {
        RejectEmptySsid,
        RejectSsidTooLong,
        RejectPasswordTooLong,
        Persisted
    };

    // 802.11 hard limits: SSID is at most 32 bytes, a WPA passphrase at most 63 characters.
    // Previously unenforced - an over-long value from a client that isn't the WiFi setup page
    // itself (nothing else validates it) would get persisted and then simply never be able to
    // join (WiFi.begin() silently truncates or rejects it), stranding the device in AP mode
    // with credentials that look "saved" but can never work.
    static constexpr size_t MAX_SSID_LENGTH = 32;
    static constexpr size_t MAX_PASSWORD_LENGTH = 63;

    // Persists the given credentials, rejecting an empty or over-long SSID or an over-long
    // password. Does not connect - see the file header: the actual join is attempted on the
    // next boot, off the async_tcp task. Never touches previously-stored credentials until the
    // new ones pass validation.
    SaveResult saveNewCredentials(const String& ssid, const String& password)
    {
        if (ssid.length() == 0) return SaveResult::RejectEmptySsid;
        if (ssid.length() > MAX_SSID_LENGTH) return SaveResult::RejectSsidTooLong;
        if (password.length() > MAX_PASSWORD_LENGTH) return SaveResult::RejectPasswordTooLong;

        store.saveCredentials(ssid, password);
        return SaveResult::Persisted;
    }

    // Attempts to connect using whatever credentials are currently stored.
    // Returns true if connected; false means no credentials were stored, or
    // the stored credentials failed to connect - either way the caller
    // should fall back to AP mode.
    bool connectUsingStoredCredentials()
    {
        String ssid, password;
        if (!store.loadCredentials(ssid, password)) return false;
        return connector.connect(ssid.c_str(), password.c_str());
    }

    void clearStoredCredentials() { store.clearCredentials(); }

   private:
    IWiFiConnector& connector;
    IPreferencesStore& store;
};
