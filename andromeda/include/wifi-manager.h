#pragma once

#include <WString.h>

// Orchestration logic for WiFi credential save/load, extracted out of Comms
// so it can be exercised natively without WiFi/Preferences hardware. All
// blocking network I/O and NVS access lives behind these two interfaces;
// WifiManager itself never calls delay()/millis() or touches WiFi/Preferences
// directly, so a test-double IWiFiConnector can return a scripted result
// instantly instead of polling in real time.

class IWiFiConnector
{
   public:
    virtual ~IWiFiConnector() = default;

    // Blocking, used for previously-stored credentials at boot. Commits to
    // station mode on success (configures a static IP, sets up
    // auto-reconnect). Real implementations poll WiFi.status() with a
    // delay()/millis()-based timeout internally; this call either returns
    // once connected or once it's given up.
    virtual bool connect(const char* ssid, const char* password) = 0;

    // Blocking, used to validate a newly-submitted credential pair before
    // persisting it. Never commits to station mode - real implementations
    // revert to broadcasting the setup access point (via enterAPMode())
    // before returning, regardless of outcome.
    virtual bool testConnection(const char* ssid, const char* password) = 0;

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
        Persisted,
        RejectConnectionFailed
    };

    // Validates, attempts to connect with the given credentials, and - only
    // on success - persists them. Never touches previously-stored credentials.
    SaveResult saveNewCredentials(const String& ssid, const String& password)
    {
        if (ssid.length() == 0) return SaveResult::RejectEmptySsid;

        if (!connector.testConnection(ssid.c_str(), password.c_str()))
            return SaveResult::RejectConnectionFailed;

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
