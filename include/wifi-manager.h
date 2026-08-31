#pragma once

#include <WString.h>

// Orchestration logic for WiFi credential save/load, extracted out of Comms
// so it can be exercised natively without WiFi/Preferences hardware. All
// blocking network I/O and NVS access lives behind these two interfaces;
// WifiManager itself never calls delay()/millis() or touches WiFi/Preferences
// directly, so a test-double IWiFiConnector can return a scripted result
// instantly instead of polling in real time.
//
// Submitting new credentials is a two-step dance, split precisely so the
// blocking half never runs on the async_tcp task (a 10-15s WiFi.status() poll
// there starves the task past its watchdog and panics the device, #114):
//   1. validateNewCredentials() - pure length checks, safe to call straight
//      from the /save HTTP handler.
//   2. testAndPersistCredentials() - blocking connection probe that persists
//      only on success. MUST be driven from a dedicated worker task (see
//      Comms::saveWorkerTask); the still-connected setup client polls
//      /save-status for the outcome.

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

    // Blocking probe of a candidate credential pair, used to give the WiFi
    // setup page inline "bad password" feedback before anything is persisted.
    // Real implementations poll WiFi.status() to a timeout and then revert to
    // the setup AP (via enterAPMode()) regardless of outcome, so the
    // still-connected setup client can read the result. MUST be called from a
    // dedicated worker task, never the async_tcp task (#114).
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
        RejectSsidTooLong,
        RejectPasswordTooLong,
        Accepted
    };

    // 802.11 hard limits: SSID is at most 32 bytes, a WPA passphrase at most 63 characters.
    // Previously unenforced - an over-long value from a client that isn't the WiFi setup page
    // itself (nothing else validates it) would get persisted and then simply never be able to
    // join (WiFi.begin() silently truncates or rejects it), stranding the device in AP mode
    // with credentials that look "saved" but can never work.
    static constexpr size_t MAX_SSID_LENGTH = 32;
    static constexpr size_t MAX_PASSWORD_LENGTH = 63;

    // Pure, non-blocking validation of a submitted credential pair: rejects an empty or
    // over-long SSID or an over-long password, otherwise Accepted. Safe to call straight from
    // the /save HTTP handler - it neither connects nor persists. Step 1 of 2; see the file
    // header.
    SaveResult validateNewCredentials(const String& ssid, const String& password) const
    {
        if (ssid.length() == 0) return SaveResult::RejectEmptySsid;
        if (ssid.length() > MAX_SSID_LENGTH) return SaveResult::RejectSsidTooLong;
        if (password.length() > MAX_PASSWORD_LENGTH) return SaveResult::RejectPasswordTooLong;
        return SaveResult::Accepted;
    }

    // Step 2 of 2: blocking connection probe, persisting the pair only if it actually joined.
    // Returns true on success (credentials now stored), false if the probe failed to connect
    // (nothing persisted; previously-stored credentials left untouched). Blocks for as long as
    // the connector's probe does - MUST be driven from a worker task, never the async_tcp task
    // (#114). Callers pass the already-validated ssid/password from validateNewCredentials().
    bool testAndPersistCredentials(const String& ssid, const String& password)
    {
        if (!connector.testConnection(ssid.c_str(), password.c_str())) return false;
        store.saveCredentials(ssid, password);
        return true;
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
