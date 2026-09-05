#include "comms.h"

#include <DNSServer.h>
#include <esp_system.h>
#include <mdns.h>
#include <sys/stat.h>

#include <cstring>

#include "fs-health.h"
#include "mdns-hosts.h"
#include "ota-config.h"
#include "ota-updater.h"
#include "version.h"
#include "ws-command-parser.h"

constexpr int DNS_PORT = 53;

namespace
{
// Builds a single-entry mdns_ip_addr_t list for `ip` - the shape
// mdns_delegate_hostname_add() takes for a delegated host's address (see
// framework-arduinoespressif32's mdns.h). IPv4 only: the device has no IPv6
// story anywhere else in the firmware either.
mdns_ip_addr_t makeMdnsIp(IPAddress ip)
{
    mdns_ip_addr_t addr{};
    addr.addr.type = ESP_IPADDR_TYPE_V4;
    addr.addr.u_addr.ip4.addr = static_cast<uint32_t>(ip);
    addr.next = nullptr;
    return addr;
}

// (Re)points every delegated hostname in `hosts[0..n)` at `ip`, skipping whichever one equals
// `primaryHostname` (the name MDNS.begin() already owns and probes for itself - never one of
// the resolved fallbacks, see startMdns()'s "never rename the primary" comment, so it isn't
// necessarily hosts[0]). A delegate that's already registered from an earlier call must be
// removed before it can be re-added with a new address; mdns_delegate_hostname_add() on an
// existing name just fails (ESP_ERR_INVALID_ARG), it doesn't update it in place.
void registerMdnsDelegates(const char* primaryHostname, const char* hosts[], size_t n, IPAddress ip)
{
    mdns_ip_addr_t addr = makeMdnsIp(ip);
    for (size_t i = 0; i < n; i++)
    {
        if (strcmp(hosts[i], primaryHostname) == 0) continue;
        if (mdns_hostname_exists(hosts[i])) mdns_delegate_hostname_remove(hosts[i]);
        esp_err_t err = mdns_delegate_hostname_add(hosts[i], &addr);
        if (err != ESP_OK)
            Log.warningln("mDNS: could not delegate hostname '%s' (err %d)", hosts[i],
                          static_cast<int>(err));
    }
}

// How long to wait for a reply before concluding a candidate hostname is free (issue #210).
// Responses to a query for a hostname's A record aren't subject to mDNS's usual 20-120ms
// shared-record jitter (RFC 6762 5.2 - that delay only applies to shared records, and a
// hostname's own address record is unique to its owner), so a genuinely-taken name answers
// almost immediately; 100ms leaves headroom for normal WiFi latency without adding much to
// boot time (3 names checked serially, so ~300ms worst case when everything is free).
constexpr uint32_t MDNS_AVAILABILITY_TIMEOUT_MS = 100;

// True if some other device on the network is already answering for `host`. Only meaningful
// once actually joined to a real network (see startMdns()'s WL_CONNECTED gate) - calling this
// against a name this device itself hasn't registered yet, which is exactly how startMdns()
// uses it (always before registering anything for that name), so it can never just find itself.
bool isHostnameTaken(const String& host)
{
    esp_ip4_addr_t addr{};
    return mdns_query_a(host.c_str(), MDNS_AVAILABILITY_TIMEOUT_MS, &addr) == ESP_OK;
}
}  // namespace

// Silent "does this file exist" check. LittleFSImpl::exists() (and the
// AsyncFileResponse path behind request->send(LittleFS, ...)) is just
// open(path, "r"), which logs a VFS error at level E for every miss - and
// the Logs page polls the log routes every 5s (#212), so a not-yet-rotated
// /log1.txt would spew "does not exist" lines forever. A bare stat() on the
// mount path checks without opening. LittleFS.begin() (main.cpp) takes the
// default "/littlefs" mount point.
static bool fileExistsQuiet(const char* littleFsPath)
{
    char full[40];
    snprintf(full, sizeof(full), "/littlefs%s", littleFsPath);
    struct stat st;
    return stat(full, &st) == 0;
}

// OtaUpdater::State -> the lowercase token the Advanced page's poll loop
// switches on. Kept here (not in ota-updater.h) so the enum stays an
// implementation detail of the updater.
static const char* otaStateToken(OtaUpdater::State s)
{
    switch (s)
    {
        case OtaUpdater::State::Idle:
            return "idle";
        case OtaUpdater::State::Checking:
            return "checking";
        case OtaUpdater::State::UpToDate:
            return "uptodate";
        case OtaUpdater::State::UpdateAvailable:
            return "available";
        case OtaUpdater::State::Downloading:
            return "downloading";
        case OtaUpdater::State::WritingFw:
            return "writing-fw";
        case OtaUpdater::State::WritingFs:
            return "writing-fs";
        case OtaUpdater::State::Rebooting:
            return "rebooting";
        case OtaUpdater::State::Failed:
            return "failed";
    }
    return "idle";
}

// Answers a request that would otherwise touch LittleFS while it's unmounted for an OTA
// filesystem write (fs-health.h) - a use-after-unmount in the VFS layer, not just a 404.
// Shared by every route below that reads from LittleFS.
static bool rejectIfFsUnavailable(AsyncWebServerRequest* r)
{
    if (mayServeFromFs()) return false;
    r->send(503, "text/plain", "filesystem unavailable during firmware update");
    return true;
}

// Serves a LittleFS text file if present, else an empty 200 - callers are
// the log routes, where "file not created yet" is normal and the Advanced
// page treats an empty body as an empty log.
static void serveTextFileOrEmpty(AsyncWebServerRequest* r, const char* path)
{
    if (rejectIfFsUnavailable(r)) return;
    if (fileExistsQuiet(path))
        r->send(LittleFS, path, "text/plain");
    else
        r->send(200, "text/plain", "");
}

// Serves a static LittleFS asset - the STATIC_FILE_ROUTE macro below and the "/" and
// "/wifi" routes (which don't fit that macro's single-path shape) all funnel through this
// so the fs-unavailable-during-OTA gate lives in exactly one place.
static void serveStaticFile(AsyncWebServerRequest* r, const char* path, const char* contentType)
{
    if (rejectIfFsUnavailable(r)) return;
    r->send(LittleFS, path, contentType);
}

// Narrow stopgap against the classic drive-by CSRF one-click case: a foreign page's
// <form> auto-submitted from a browser on the same LAN, targeting POST /save or /reset.
// Neither route requires a CORS preflight (simple form POSTs never trigger one), so
// nothing else here would ever see or block it. A cross-origin browser POST always
// carries an Origin header that won't match this device's own host; a request with none
// at all (curl, a script, same-origin fetch from this device's own pages) is let
// through unchanged. There's no other auth on this device at all (#85, tracked
// separately) - this only closes the "just visiting an unrelated web page" case, not a
// deliberate attacker who already knows the device's address.
static bool isCrossOriginPost(AsyncWebServerRequest* request)
{
    if (!request->hasHeader("Origin")) return false;
    const AsyncWebHeader* origin = request->getHeader("Origin");
    return !originMatchesHost(origin->value().c_str(), request->host().c_str());
}

#define STATIC_FILE_ROUTE(path, contentType) \
    server.on(path, HTTP_GET,                \
              [](AsyncWebServerRequest* request) { serveStaticFile(request, path, contentType); })

Comms::Comms()
    : server(80),
      ws("/ws"),
      webServerTaskHandle(nullptr),
      dnsServer(nullptr),
      isAPMode(false),
      realWifiManager(wifiConnector, preferencesStore),
      wifiManager(&realWifiManager),
      scanInProgress(false),
      scanComplete(false),
      scanResults(""),
      scanResultsMutex(xSemaphoreCreateMutex()),
      lastScanTime(0),
      lastBroadcastMs(0)
{
}

Comms::SetupOutcome Comms::setup()
{
    WifiManager::ConnectResult result = wifiManager->connectUsingStoredCredentials();
    if (result == WifiManager::ConnectResult::Connected)
    {
        startStationMode();
        return SetupOutcome::Connected;
    }

    Log.noticeln("Starting AP mode for WiFi configuration");
    startAPMode();
    return result == WifiManager::ConnectResult::NeverConfigured ? SetupOutcome::NeverConfigured
                                                                 : SetupOutcome::ConnectFailed;
}

String Comms::mdnsHostsJson()
{
    // Before startMdns() has ever run (a fresh AP-mode boot's setup page can ask before the
    // radio's done any work at all - see this function's header comment) there's nothing
    // resolved yet: fall back to the plain, unchecked defaults, same as an AP-only boot ends up
    // resolving anyway (see startMdns()'s WL_CONNECTED gate).
    String deviceHost =
        resolvedDeviceHost.length() ? resolvedDeviceHost : DeviceIdentity::getMdnsHostname();
    String modelHost =
        resolvedModelHost.length() ? resolvedModelHost : DeviceIdentity::getDefaultMdnsHostname();
    String andromedaHost =
        resolvedAndromedaHost.length() ? resolvedAndromedaHost : String("andromeda");

    const char* hosts[MdnsHosts::MAX_HOSTS];
    size_t n = MdnsHosts::buildHostList(deviceHost.c_str(), modelHost.c_str(),
                                        andromedaHost.c_str(), hosts);

    String json = "[";
    for (size_t i = 0; i < n; i++)
    {
        if (i > 0) json += ",";
        json += "\"" + String(hosts[i]) + ".local\"";
    }
    json += "]";
    return json;
}

// Starts the mDNS responder (idempotent - MDNS.begin() only ever runs once per boot) and
// (re)registers the delegated fallback hostnames against `ip`. Called from
// startStationMode(), from beginAPBroadcast() (so the same names work on the setup AP), and
// again on every ARDUINO_EVENT_WIFI_STA_GOT_IP once station mode is up, since a DHCP renewal
// can hand back a different address and the delegated list (unlike the primary hostname) is
// static until explicitly refreshed.
void Comms::startMdns(IPAddress ip)
{
    if (!mdnsStarted)
    {
        String deviceHost = DeviceIdentity::getMdnsHostname();
        primaryMdnsHostname = deviceHost;

        // Availability can only be checked once there's a real network to ask - an AP-mode-only
        // boot (WiFi.status() != WL_CONNECTED there) has nothing else to collide with, and
        // that's also exactly the case where /device-info's "hosts" list gets asked for before
        // this ever runs (issue #210), so there'd be nothing to show for the checked state
        // anyway. mdns_init() runs early (idempotent - MDNS.begin() below calls it again) so
        // mdns_query_a() has a responder to query against before this device claims any name of
        // its own; querying only ever targets names not yet registered to us (each check runs
        // before that pair's winner is registered anywhere), so a query can never just find
        // ourselves.
        bool checkAvailability = WiFi.status() == WL_CONNECTED;
        if (checkAvailability)
        {
            mdns_init();
            resolvedDeviceHost =
                MdnsHosts::resolveFallback(isHostnameTaken(deviceHost), deviceHost.c_str(),
                                           (deviceHost + "-" + DeviceIdentity::getUid()).c_str());

            String modelUidHost = DeviceIdentity::getDefaultMdnsHostname();
            resolvedModelHost =
                MdnsHosts::resolveFallback(isHostnameTaken(modelUidHost), modelUidHost.c_str(),
                                           DeviceIdentity::getModelMdnsHostname().c_str());

            resolvedAndromedaHost =
                MdnsHosts::resolveFallback(isHostnameTaken("andromeda"), "andromeda",
                                           DeviceIdentity::getAndromedaMdnsHostname().c_str());
        }
        else
        {
            resolvedDeviceHost = deviceHost;
            resolvedModelHost = DeviceIdentity::getDefaultMdnsHostname();
            resolvedAndromedaHost = "andromeda";
        }

        // Worth a couple of retries rather than giving up on the first transient failure and
        // leaving the device unreachable by name for the rest of the boot. Runs before the web
        // server task exists in the station-mode path, so blocking briefly here is harmless
        // (same reasoning as WifiManager's connect()); in the AP-mode path it can also run at
        // runtime from the WiFi-recovery monitor task, off the render loop. Always registers
        // `deviceHost` as-is, even if the availability check above found it taken - resolving
        // that would mean actively renaming the device's own primary identity out from under a
        // running ESP-IDF mDNS instance, not just picking what else to list/delegate; simpler
        // and safer to leave the primary alone and only steer the delegated/display list (see
        // registerMdnsDelegates()) toward the free alternative.
        bool ok = false;
        for (int attempt = 0; !ok && attempt < 3; attempt++)
        {
            if (attempt > 0) delay(200);
            ok = MDNS.begin(deviceHost.c_str());
        }
        if (!ok)
        {
            Log.errorln("MDNS failed to start after retries");
            return;
        }
        mdnsStarted = true;

        // _http._tcp only on the primary hostname - see registerMdnsDelegates()'s comment for
        // why the delegated fallback names don't each get their own service record (a browse
        // listing should show this device once, not once per name it also answers to).
        MDNS.addService("http", "tcp", 80);
        MDNS.addServiceTxt("http", "tcp", "uid", DeviceIdentity::getUid());
        const ModelConfig* config = GEOMETRY.getConfig();
        MDNS.addServiceTxt("http", "tcp", "model", config ? config->name : "Unknown");

        // Registered once, mirroring startAsyncScan()'s handlerRegistered guard just below -
        // WiFi.onEvent() appends rather than replaces, so registering this on every
        // startMdns() call (itself re-entrant via beginAPBroadcast()) would fire once per
        // past call on every future renewal.
        static bool gotIpHandlerRegistered = false;
        if (!gotIpHandlerRegistered)
        {
            gotIpHandlerRegistered = true;
            WiFi.onEvent(
                [](WiFiEvent_t e, WiFiEventInfo_t)
                {
                    if (e != ARDUINO_EVENT_WIFI_STA_GOT_IP) return;
                    Comms::Instance().startMdns(WiFi.localIP());
                });
        }
    }

    const char* hosts[MdnsHosts::MAX_HOSTS];
    size_t n = MdnsHosts::buildHostList(resolvedDeviceHost.c_str(), resolvedModelHost.c_str(),
                                        resolvedAndromedaHost.c_str(), hosts);
    registerMdnsDelegates(primaryMdnsHostname.c_str(), hosts, n, ip);
}

// Shared by startAPMode() (boot-time fallback) and enterAPFallbackMode() (runtime fallback
// after a mid-run disconnect): flips the radio into AP mode and (re)starts the captive-portal
// DNS server. Does not touch the web server task - see enterAPFallbackMode()'s comment.
void Comms::beginAPBroadcast()
{
    // Computed/allocated/started before the lock is taken - see apStateMux's comment in
    // comms.h for why nothing that can allocate or block runs while holding it.
    String newName = DeviceIdentity::getDeviceName();
    wifiConnector.enterAPMode();

    IPAddress apIP = WiFi.softAPIP();
    Log.noticeln("AP Mode started. IP: %s", apIP.toString().c_str());

    // So the device's permanent names (include/mdns-hosts.h) also resolve while parked on
    // its own setup AP, not just once it's joined a real network.
    startMdns(apIP);

    DNSServer* newDns = new DNSServer();
    newDns->start(DNS_PORT, "*", apIP);

    portENTER_CRITICAL(&apStateMux);
    DNSServer* oldDns = dnsServer;
    dnsServer = newDns;
    isAPMode = true;
    strncpy(runningDeviceName, newName.c_str(), sizeof(runningDeviceName) - 1);
    runningDeviceName[sizeof(runningDeviceName) - 1] = '\0';
    portEXIT_CRITICAL(&apStateMux);

    // beginAPBroadcast() only ever runs once per boot in practice (isAPMode has no path
    // back to false without a reboot, and enterAPFallbackMode() early-returns once already
    // in AP mode), so oldDns is always nullptr here - deleted defensively anyway, and
    // outside the lock, in case that invariant ever changes.
    delete oldDns;

    // Covers both ways the device ends up here: a failed boot-time join
    // (Comms::setup() -> startAPMode()) and a mid-run outage that outlasted
    // enterAPFallbackMode()'s dead time - either way, this is what stops it being a
    // one-way door into the setup AP.
    startApRejoinMonitor();
}

bool Comms::isInAPMode() const
{
    portENTER_CRITICAL(&apStateMux);
    bool result = isAPMode;
    portEXIT_CRITICAL(&apStateMux);
    return result;
}

bool Comms::startAPMode()
{
    beginAPBroadcast();
    createWebServerTask();

    // Background scan for setup UI
    xTaskCreate(
        [](void* p)
        {
            vTaskDelay(2000 / portTICK_PERIOD_MS);
            Comms::Instance().startAsyncScan();
            vTaskDelete(NULL);
        },
        "InitScan", 2048, nullptr, 1, nullptr);

    return true;
}

void Comms::enterAPFallbackMode()
{
    if (isAPMode) return;

    Log.warningln("WiFi unreachable for too long, falling back to AP mode for reconfiguration");
    beginAPBroadcast();

    // The web server task is already running (createWebServerTask() ran at boot in
    // startStationMode()) and its loop already checks isAPMode/dnsServer every tick to decide
    // whether to service the captive portal - nothing else to start. Kick a background scan
    // so the setup page has networks to show as soon as someone connects to the AP.
    xTaskCreate(
        [](void* p)
        {
            vTaskDelay(2000 / portTICK_PERIOD_MS);
            Comms::Instance().startAsyncScan();
            vTaskDelete(NULL);
        },
        "APFallbackScan", 2048, nullptr, 1, nullptr);
}

bool Comms::startStationMode()
{
    // No lock needed here: this only ever runs at boot, before the web server task (the
    // only concurrent reader of these fields) exists - unlike beginAPBroadcast(), which
    // is also reachable at runtime from the WifiRecovery monitor task.
    isAPMode = false;
    String newName = DeviceIdentity::getDeviceName();
    strncpy(runningDeviceName, newName.c_str(), sizeof(runningDeviceName) - 1);
    runningDeviceName[sizeof(runningDeviceName) - 1] = '\0';

    // mDNS is the primary way to reach the device now that station mode uses DHCP instead of
    // a fixed IP (see EspWiFiConnector::connect()) - see startMdns() for the retry/delegate
    // logic. Runs before the web server task exists, so blocking briefly here is harmless
    // (same reasoning as WifiManager's connect()).
    startMdns(WiFi.localIP());

    createWebServerTask();
    printWifiStatus();
    return true;
}

void Comms::createWebServerTask()
{
    xTaskCreatePinnedToCore(webServerTask, "WebServer", 16384, this, 1, &webServerTaskHandle, 0);
}

void Comms::webServerTask(void* parameter)
{
    Comms* comms = static_cast<Comms*>(parameter);
    comms->setupRoutes();
    comms->server.begin();
    Log.noticeln("Web server started on Core %d", xPortGetCoreID());

    while (true)
    {
        // Snapshot both fields under the lock, then act on the snapshot outside it - see
        // apStateMux's comment in comms.h. dnsServer is never deleted with a reader
        // possibly still using it in practice (beginAPBroadcast() only runs once per
        // boot), so holding a raw pointer past the lock here is safe.
        portENTER_CRITICAL(&comms->apStateMux);
        bool apMode = comms->isAPMode;
        DNSServer* dns = comms->dnsServer;
        portEXIT_CRITICAL(&comms->apStateMux);
        if (apMode && dns) { dns->processNextRequest(); }

        comms->broadcastStateIfDirty();
        comms->pushMetricsIfDue();
        // Bounds the connected-client list to DEFAULT_MAX_WS_CLIENTS - previously never
        // called anywhere, so nothing pruned stale/excess clients and each one holds its own
        // send queue of multi-KB state JSON on a device with no PSRAM.
        comms->ws.cleanupClients();
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void Comms::setupRoutes()
{
    // WebSocket endpoint
    ws.onEvent(
        [](AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg,
           uint8_t* data, size_t len)
        {
            if (type == WS_EVT_CONNECT)
            {
                // The WebSocket handshake is a plain GET, so a browser on an
                // unrelated LAN-reachable page can open ws://<device>/ws with
                // no CORS preflight and then send {"type":"reboot"} etc. -
                // bypassing every isCrossOriginPost() guard on the HTTP
                // routes. Reject a handshake whose Origin doesn't match this
                // device's own host. For WS_EVT_CONNECT the event arg is the
                // upgrade request; a request with no Origin (a native client,
                // a same-origin page) is allowed, same policy as the POST
                // routes.
                auto* req = reinterpret_cast<AsyncWebServerRequest*>(arg);
                if (req && req->hasHeader("Origin") &&
                    !originMatchesHost(req->getHeader("Origin")->value().c_str(),
                                       req->host().c_str()))
                {
                    Log.warningln("Rejecting cross-origin WebSocket handshake");
                    client->close();
                    return;
                }

                // Auto-ping every 30s of silence. Without this, a WiFi drop that doesn't
                // produce a clean TCP FIN (the normal case for the device losing power or
                // moving out of range - not a client-side "close the tab") leaves the
                // client's readyState stuck at OPEN for minutes: no error/close event fires,
                // the UI never shows as disconnected, and every button press silently does
                // nothing. This makes the library itself notice a dead peer (a failed/
                // unacked ping closes the connection) instead of relying only on the
                // client-side staleness timer in controls.js.
                client->keepAlivePeriod(30);

                char buf[WsStateBuilder::JSON_CAPACITY];
                size_t stateLen = Comms::Instance().buildCurrentStateJson(buf, sizeof(buf));
                if (stateLen) client->text(buf, stateLen);
            }
            else if (type == WS_EVT_DATA)
            {
                AwsFrameInfo* info = (AwsFrameInfo*)arg;
                if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
                {
                    // Copy into a bounded, NUL-terminated local buffer. `data`
                    // points into the lwIP pbuf payload, so the previous
                    // `data[len] = 0` wrote one byte past it. Every real
                    // command message is well under this; a longer frame
                    // isn't one and is dropped.
                    char json[256];
                    if (len >= sizeof(json))
                    {
                        Log.warningln("Ignoring oversized WS message (%u bytes)",
                                      static_cast<unsigned>(len));
                        return;
                    }
                    memcpy(json, data, len);
                    json[len] = 0;

                    MissionControl& mc = MissionControl::Instance();

                    // BRIGHTNESS and live COLOR updates fire at drag speed (dozens/sec) -
                    // routing every one through the command queue overruns it and starves
                    // async_tcp with per-command logging. Apply them directly; they're simple
                    // field writes the render task re-reads every frame, same as before
                    // commands were queue-only. Everything else stays queued since it mutates
                    // render-task-owned state (allocating effects, etc.).
                    uint8_t brightnessValue;
                    bool brightnessCommit;
                    Command command;
                    bool wantSubscribe;
                    WsCommandParser::Topic subscriptionTopic;
                    if (WsCommandParser::parseSubscription(json, wantSubscribe, subscriptionTopic))
                    {
                        // Only "metrics" exists today (see ws-command-parser.h's Topic enum),
                        // but the dispatch is written to fall through to "unrecognized" for any
                        // future topic parseSubscription() doesn't already reject itself, rather
                        // than silently doing nothing.
                        if (subscriptionTopic == WsCommandParser::Topic::Metrics)
                        {
                            if (wantSubscribe)
                                Comms::Instance().addMetricsSubscriber(client->id());
                            else
                                Comms::Instance().removeMetricsSubscriber(client->id());
                        }
                    }
                    else if (WsCommandParser::parseBrightness(json, brightnessValue,
                                                              brightnessCommit))
                    {
                        mc.setMaxBrightness(brightnessValue);
                        if (brightnessCommit) BrightnessConfig::persist(brightnessValue);
                    }
                    else if (WsCommandParser::parse(json, command))
                    {
                        if (command.type == CommandType::COLOR && mc.isColorActive())
                        {
                            mc.setLiveColor(command.r, command.g, command.b);
                            // Same commit-gating as BRIGHTNESS above - only the drag-release
                            // message persists. This is the common case for a color commit:
                            // by the time a drag ends, color mode is already active, so the
                            // message never touches the queue path below (which handles the
                            // same flag for the rarer case of a commit landing on the very
                            // message that first switches into color mode).
                            if (command.colorCommit)
                                StartupStateConfig::persistMode(
                                    StartupStateConfig::Mode::HoldingColor, 0, command.r, command.g,
                                    command.b);
                        }
                        else
                            mc.queueWebCommand(command);
                    }
                    else
                    {
                        // ArduinoLog's format strings only support a single character after
                        // '%' (no printf-style width/precision - see ArduinoLog.cpp's
                        // printFormat()), so truncating has to happen on the string itself
                        // before logging it, not via the format spec. Bounded because `json`
                        // is an unvalidated, client-controlled message written into the 16KB
                        // rotating log - previously unbounded, so a client (accidentally or
                        // not - no auth on this route) sending large garbage frames could
                        // flush the real diagnostic history in a handful of messages.
                        char truncated[129];
                        snprintf(truncated, sizeof(truncated), "%s", json);
                        Log.warningln("Unrecognized WS command message: %s", truncated);
                    }
                }
                else
                {
                    // Multi-packet/fragmented frames (info->index != 0, or a partial
                    // info->len != len) and non-text opcodes previously hit neither branch -
                    // silently dropped with not even a log line. This client only ever sends
                    // small single-frame messages, so it's not expected in practice, but a
                    // proxy or a future non-firmware client fragmenting a message deserves a
                    // trace instead of vanishing without one.
                    Log.warningln(
                        "Dropped WS frame (final=%d index=%u len=%u opcode=%d) - fragmented "
                        "or non-text frames aren't supported",
                        (int)info->final, (unsigned long)info->index, (unsigned long)len,
                        (int)info->opcode);
                }
            }
            else if (type == WS_EVT_DISCONNECT || type == WS_EVT_ERROR)
            {
                // Neither event was handled at all before #214's metrics subscription: a
                // client that disconnects (clean close) or errors out (dead peer, e.g. the
                // auto-ping above going unacked) must stop being pushed metrics, or
                // pushMetricsIfDue() keeps calling ws.text() against a dead id forever - not
                // harmful (AsyncWebSocket no-ops a send to a gone id) but a permanently wasted
                // slot in the fixed-size subscriber array. Belt-and-braces: pushMetricsIfDue()
                // also self-prunes via ws.hasClient() for the case a client vanishes without
                // either event firing (cleanupClients() pruning it directly).
                Comms::Instance().removeMetricsSubscriber(client->id());
            }
        });
    server.addHandler(&ws);

    // Global static files
    server.on("/", HTTP_GET,
              [](AsyncWebServerRequest* r) { serveStaticFile(r, "/index.html", "text/html"); });

    // TODO: I am confident there must exist a better way.
    // Virtually certain the ESPAsyncWebServer can serve directly from LittleFS with the correct
    // content type
    STATIC_FILE_ROUTE("/index.html", "text/html");
    STATIC_FILE_ROUTE("/wifi-setup.html", "text/html");
    STATIC_FILE_ROUTE("/favicon.svg", "image/svg+xml");
    STATIC_FILE_ROUTE("/fonts/cinzel.woff2", "font/woff2");
    STATIC_FILE_ROUTE("/js/utils.js", "application/javascript");
    STATIC_FILE_ROUTE("/js/ws-client-utils.js", "application/javascript");
    STATIC_FILE_ROUTE("/js/controls-logic.js", "application/javascript");
    STATIC_FILE_ROUTE("/js/controls.js", "application/javascript");
    STATIC_FILE_ROUTE("/js/wifi.js", "application/javascript");
    STATIC_FILE_ROUTE("/js/wifi-logic.js", "application/javascript");
    STATIC_FILE_ROUTE("/js/advanced-logic.js", "application/javascript");
    STATIC_FILE_ROUTE("/js/advanced.js", "application/javascript");
    STATIC_FILE_ROUTE("/js/device-name.js", "application/javascript");
    STATIC_FILE_ROUTE("/js/logs-logic.js", "application/javascript");
    STATIC_FILE_ROUTE("/js/logs.js", "application/javascript");
    STATIC_FILE_ROUTE("/css/common.css", "text/css");
    STATIC_FILE_ROUTE("/css/controls.css", "text/css");
    STATIC_FILE_ROUTE("/css/wifi.css", "text/css");
    STATIC_FILE_ROUTE("/css/advanced.css", "text/css");
    STATIC_FILE_ROUTE("/css/logs.css", "text/css");
    STATIC_FILE_ROUTE("/advanced.html", "text/html");
    STATIC_FILE_ROUTE("/device-name.html", "text/html");
    STATIC_FILE_ROUTE("/logs.html", "text/html");

    server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest* r)
              { serveStaticFile(r, "/wifi-setup.html", "text/html"); });

    // Log files, served from LittleFS via serveTextFileOrEmpty so a
    // not-yet-created file returns an empty 200 instead of letting the FS
    // layer log a VFS error on the miss (see fileExistsQuiet above). The
    // logger keeps the log as these two rotating files; /log1.txt has no
    // content until the first 16 KB rotation (SimpleFileLog::DEFAULT_MAX_LOG_BYTES,
    // loggers.h). Keep the literals in sync with LOG_FILE_CUR / LOG_FILE_OLD
    // (utils.h). "/logs" here is the raw current-file alias fetched by curl/etc -
    // it's distinct from "/logs.html" above, the browsable log viewer page (#212).
    server.on("/log0.txt", HTTP_GET,
              [](AsyncWebServerRequest* r) { serveTextFileOrEmpty(r, LOG_FILE_CUR); });
    server.on("/log1.txt", HTTP_GET,
              [](AsyncWebServerRequest* r) { serveTextFileOrEmpty(r, LOG_FILE_OLD); });
    server.on("/logs", HTTP_GET,
              [](AsyncWebServerRequest* r) { serveTextFileOrEmpty(r, LOG_FILE_CUR); });

    server.on("/metrics", HTTP_GET,
              [](AsyncWebServerRequest* r)
              {
                  // Same builder pushMetricsIfDue() uses for the WebSocket "metrics" frame (#214) -
                  // HTTP and WS can't drift apart. Always the full (static+volatile+OTA) shape,
                  // same as this route always answered before the WS push existed.
                  WsMetricsBuilder::MetricsSnapshot snapshot{};
                  Comms::Instance().fillMetricsSnapshot(snapshot, /*includeStatic=*/true,
                                                        /*includeOta=*/true);
                  // +1 so buildMetricsJson (which, like ArduinoJson's own serializeJson(doc, char*,
                  // size_t), writes up to `len` bytes but never NUL-terminates) always has room for
                  // the terminator send()'s implicit String(const char*) construction needs below.
                  char json[WsMetricsBuilder::JSON_CAPACITY + 1];
                  size_t len = WsMetricsBuilder::buildMetricsJson(snapshot, json, sizeof(json) - 1);
                  if (len)
                  {
                      json[len] = '\0';
                      r->send(200, "application/json", json);
                  }
                  else
                      r->send(500, "text/plain", "metrics serialization failed");
              });

    // Shared Config & Monitoring
    server.on("/fps", HTTP_GET, [](AsyncWebServerRequest* r)
              { r->send(200, "text/plain", String(PerformanceMonitor::Instance().fps())); });
    // Read before the WiFi setup page ever submits credentials (issue #135) - the WS state
    // feed isn't available yet in AP mode, and by the time /save-status resolves the setup
    // AP may already be gone (on iOS, immediately - see data/js/wifi.js). Answers the same
    // in both AP and station mode: DeviceIdentity's derivations don't depend on which one is
    // currently active.
    server.on("/device-info", HTTP_GET,
              [](AsyncWebServerRequest* r)
              {
                  // connectedSsid lets the WiFi setup page (#210) show the already-joined
                  // network directly instead of always kicking off a fresh scan on load -
                  // empty when in AP mode (WiFi.SSID() returns "" there).
                  bool connected = WiFi.status() == WL_CONNECTED;
                  String json = "{\"name\":\"" + DeviceIdentity::getDeviceName() +
                                "\",\"defaultName\":\"" + DeviceIdentity::getDefaultName() +
                                "\",\"uid\":\"" + String(DeviceIdentity::getUid()) +
                                "\",\"hosts\":" + Comms::Instance().mdnsHostsJson() +
                                ",\"connectedSsid\":\"" +
                                (connected ? jsonEscape(WiFi.SSID()) : "") + "\"}";
                  r->send(200, "application/json", json);
              });
    server.on(
        "/brightness", HTTP_GET, [](AsyncWebServerRequest* r)
        { r->send(200, "text/plain", String(MissionControl::Instance().getMaxBrightness())); });

    // WiFi functionality
    server.on("/scan", HTTP_GET, [this](AsyncWebServerRequest* r)
              { r->send(200, "application/json", scanWiFiNetworks()); });
    server.on("/save", HTTP_POST,
              [this](AsyncWebServerRequest* r)
              {
                  if (isCrossOriginPost(r))
                  {
                      r->send(403, "text/plain", "Cross-origin request rejected");
                      return;
                  }
                  processWiFiCredentials(r, r->arg("ssid"), r->arg("password"));
              });
    // Polled by the setup page after a 202 from /save, until it reads a terminal verdict.
    // "pending" covers Idle too (the page only polls once a probe is in flight).
    server.on("/save-status", HTTP_GET,
              [this](AsyncWebServerRequest* r)
              {
                  const char* status = "pending";
                  if (saveStatus == SaveStatus::Connected)
                      status = "connected";
                  else if (saveStatus == SaveStatus::Failed)
                      status = "failed";
                  r->send(200, "text/plain", status);
              });
    server.on("/reset", HTTP_POST,
              [this](AsyncWebServerRequest* r)
              {
                  if (isCrossOriginPost(r))
                  {
                      r->send(403, "text/plain", "Cross-origin request rejected");
                      return;
                  }
                  wifiManager->clearStoredCredentials();
                  r->send(200, "text/plain", "Credentials cleared. Restarting...");
                  xTaskCreate(
                      [](void*)
                      {
                          vTaskDelay(2000 / portTICK_PERIOD_MS);
                          ESP.restart();
                      },
                      "Restart", 2048, NULL, 1, NULL);
              });

    // OTA (#63). One-shot triggers return 202 like /save; the Advanced page
    // polls GET /ota-status. All three POSTs get the same drive-by-CSRF guard
    // as /save and /reset.
    server.on("/ota", HTTP_POST,
              [](AsyncWebServerRequest* r)
              {
                  if (isCrossOriginPost(r))
                  {
                      r->send(403, "text/plain", "Cross-origin request rejected");
                      return;
                  }
                  // Answer with what actually happened: 202 only if a worker
                  // was spawned, 409/503 (with a reason) otherwise, so the
                  // Advanced page never polls a request that did nothing.
                  OtaStartGate::Outcome o = OtaUpdater::startUpdate();
                  r->send(OtaStartGate::httpStatus(o), "text/plain", OtaStartGate::message(o));
              });
    server.on("/ota-check", HTTP_POST,
              [](AsyncWebServerRequest* r)
              {
                  if (isCrossOriginPost(r))
                  {
                      r->send(403, "text/plain", "Cross-origin request rejected");
                      return;
                  }
                  OtaStartGate::Outcome o = OtaUpdater::startCheck();
                  r->send(OtaStartGate::httpStatus(o), "text/plain", OtaStartGate::message(o));
              });
    server.on("/ota-channel", HTTP_POST,
              [](AsyncWebServerRequest* r)
              {
                  if (isCrossOriginPost(r))
                  {
                      r->send(403, "text/plain", "Cross-origin request rejected");
                      return;
                  }
                  bool dev = r->arg("dev") == "true" || r->arg("dev") == "1";
                  OtaConfig::persistDevChannel(dev);
                  OtaUpdater::startCheck();  // re-evaluate against the new channel
                  r->send(200, "text/plain", dev ? "dev" : "stable");
              });
    server.on("/ota-status", HTTP_GET,
              [](AsyncWebServerRequest* r)
              {
                  OtaUpdater::Status s = OtaUpdater::status();
                  // partialFailure/partialFailureReason survive the reboot that
                  // an FS-half OTA failure forces (OtaConfig), so a fresh boot
                  // still tells the owner the filesystem didn't take. The flag
                  // is authoritative on its own; the reason is best-effort.
                  bool pfPending = OtaConfig::partialFailurePending();
                  char pfReason[96] = "";
                  OtaConfig::partialFailureReason(pfReason, sizeof(pfReason));
                  // Worst case ~380 B (error[96] + latestTag[48] + pfReason[96]
                  // all full); 512 keeps a rare long string from truncating.
                  char json[512];
                  snprintf(json, sizeof(json),
                           "{\"state\":\"%s\",\"progress\":%u,\"latestCode\":%u,"
                           "\"latestTag\":\"%s\",\"channel\":\"%s\",\"error\":\"%s\","
                           "\"partialFailure\":%s,\"partialFailureReason\":\"%s\"}",
                           otaStateToken(s.state), static_cast<unsigned>(s.progressPct),
                           static_cast<unsigned>(s.latestVersionCode), s.latestTag,
                           OtaConfig::devChannel() ? "dev" : "stable", s.error,
                           pfPending ? "true" : "false", pfReason);
                  r->send(200, "application/json", json);
              });

    // Captive Portal Detection
    server.on("/generate_204", HTTP_GET, [this](AsyncWebServerRequest* r) { r->redirect("/"); });
    server.on("/hotspot-detect.html", HTTP_GET,
              [this](AsyncWebServerRequest* r) { r->redirect("/"); });

    server.onNotFound(
        [this](AsyncWebServerRequest* request)
        {
            if (isInAPMode() && request->host() != WiFi.softAPIP().toString())
            {
                request->redirect("http://" + WiFi.softAPIP().toString() + "/");
            }
            else { request->send(404, "text/plain", "Not Found"); }
        });
}

void Comms::onWiFiScanComplete(int networksFound)
{
    String json = "{\"networks\":[";
    for (size_t i = 0; i < networksFound; i++)
    {
        if (i > 0) json += ",";
        // wifi.js has always read network.encryption (to pick the lock-icon and to
        // autofocus the password field for a non-open network) but /scan never actually
        // sent it - every network showed the literal text "undefined" on the setup page.
        const char* encryption = WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "none" : "secured";
        json += "{\"ssid\":\"" + jsonEscape(WiFi.SSID(i)) + "\",\"rssi\":" + String(WiFi.RSSI(i)) +
                ",\"encryption\":\"" + encryption + "\"}";
    }
    json += "]}";

    xSemaphoreTake(scanResultsMutex, portMAX_DELAY);
    scanResults = json;
    xSemaphoreGive(scanResultsMutex);

    scanComplete = true;
    scanInProgress = false;
    lastScanTime = millis();
    WiFi.scanDelete();
}

void Comms::startAsyncScan()
{
    if (scanInProgress) return;
    scanInProgress = true;
    scanComplete = false;
    scanStartedAt = millis();

    // A successful async start returns WIFI_SCAN_RUNNING (-1); WIFI_SCAN_FAILED
    // (-2) means esp_wifi_scan_start() refused (e.g. the STA is mid-association
    // after an AP-mode rejoin attempt) and NO SCAN_DONE event will ever fire -
    // so the handler below never clears scanInProgress and /scan answers
    // "scanning" for the rest of the session. Reap it here; the scanIsStale()
    // watchdog in scanWiFiNetworks() covers a genuinely lost event.
    if (WiFi.scanNetworks(true) == WIFI_SCAN_FAILED)
    {
        Log.warningln("WiFi scan failed to start - will retry on the next /scan");
        scanInProgress = false;
        return;
    }

    // Registered once rather than on every call: WiFi.onEvent() appends a new callback to the
    // global event list each time, and none of the previous ones are ever removed - a long
    // AP-mode session (the setup page's scan cache misses every 30s) would grow that list
    // without bound and fire every stale copy on each future SCAN_DONE.
    static bool handlerRegistered = false;
    if (!handlerRegistered)
    {
        handlerRegistered = true;
        WiFi.onEvent(
            [](WiFiEvent_t e, WiFiEventInfo_t i)
            {
                if (e != ARDUINO_EVENT_WIFI_SCAN_DONE) return;

                int n = WiFi.scanComplete();
                if (n >= 0) { Comms::Instance().onWiFiScanComplete(n); }
                else
                {
                    // A negative result is scanComplete()'s error code (e.g.
                    // WIFI_SCAN_FAILED) - onWiFiScanComplete() is what clears
                    // scanInProgress, so without this branch a failed scan left it stuck
                    // true forever and /scan returned "scanning" for the rest of the
                    // session, with no way to retry short of a reboot.
                    Log.warningln("WiFi scan failed (code %d)", n);
                    Comms::Instance().scanInProgress = false;
                }
            });
    }
}

String Comms::scanWiFiNetworks()
{
    if (isScanCacheValid(scanComplete, lastScanTime, millis(), SCAN_CACHE_MS))
    {
        xSemaphoreTake(scanResultsMutex, portMAX_DELAY);
        String result = scanResults;
        xSemaphoreGive(scanResultsMutex);
        return result;
    }
    // A scan whose SCAN_DONE event never arrived (see startAsyncScan()) would
    // otherwise keep this method returning "scanning" forever. Reap it so the
    // next line can start a fresh one.
    if (scanIsStale(scanInProgress, scanStartedAt, millis(), SCAN_STALE_MS))
    {
        Log.warningln("WiFi scan stuck for >%lus - abandoning it", SCAN_STALE_MS / 1000);
        scanInProgress = false;
    }
    if (!scanInProgress) startAsyncScan();
    if (scanInProgress) return "{\"networks\":[],\"status\":\"scanning\"}";

    xSemaphoreTake(scanResultsMutex, portMAX_DELAY);
    String result = scanResults;
    xSemaphoreGive(scanResultsMutex);
    return result;
}

// ssid/password copied off the AsyncWebServerRequest before it's freed, handed to
// saveWorkerTask() as its FreeRTOS parameter. Heap-allocated by the /save handler, deleted by
// the worker.
struct SaveJob
{
    String ssid;
    String password;
};

void Comms::saveWorkerTask(void* param)
{
    SaveJob* job = static_cast<SaveJob*>(param);
    Comms& comms = Comms::Instance();

    bool connected = comms.wifiManager->testAndPersistCredentials(job->ssid, job->password);
    String ssid = job->ssid;
    delete job;

    comms.saveStatus = connected ? SaveStatus::Connected : SaveStatus::Failed;

    if (connected)
    {
        // Credentials are persisted and verified. Give the setup client a beat to poll
        // /save-status and see "connected", then reboot - Comms::setup() re-joins with the
        // stored credentials before the web server task exists (blocking there is harmless).
        Log.noticeln("WiFi credential probe succeeded for SSID '%s' - rebooting to connect",
                     ssid.c_str());
        vTaskDelay(3000 / portTICK_PERIOD_MS);
        ESP.restart();
    }
    else
    {
        // testConnection() has already reverted the radio to the setup AP, so the client is
        // still connected and its poll will report "failed". Nothing persisted; stay put for
        // the user to correct the password and resubmit.
        Log.warningln("WiFi credential probe failed for SSID '%s' - staying in AP mode",
                      ssid.c_str());
    }

    vTaskDelete(NULL);
}

bool Comms::processWiFiCredentials(AsyncWebServerRequest* request, const String& ssid,
                                   const String& password)
{
    switch (wifiManager->validateNewCredentials(ssid, password))
    {
        case WifiManager::SaveResult::RejectEmptySsid:
            request->send(400, "text/plain", "SSID required");
            return true;

        case WifiManager::SaveResult::RejectSsidTooLong:
            request->send(400, "text/plain", "SSID too long (32 characters max)");
            return true;

        case WifiManager::SaveResult::RejectPasswordTooLong:
            request->send(400, "text/plain", "Password too long (63 characters max)");
            return true;

        case WifiManager::SaveResult::Accepted:
            // Single-flight. saveWorkerTask holds the one WiFi radio for up to
            // 10 s (WiFi.begin() + a status poll). A second accepted /save
            // while the first probe runs - a typo corrected and re-submitted,
            // or a second phone on the setup AP - would spawn a second worker
            // whose WiFi.begin() supersedes the first's; the first worker's
            // poll then sees WL_CONNECTED for the *second* network, persists
            // its OWN ssid/password, and reboots the device onto credentials
            // that can't join. /save only ever runs on the single async_tcp
            // task, so this check-then-set isn't itself racy.
            if (saveStatus == SaveStatus::Pending)
            {
                request->send(409, "text/plain", "A connection test is already in progress");
                return true;
            }

            // The blocking connection probe can't run here - this handler is on the async_tcp
            // task and a 10s WiFi.status() poll trips its watchdog (#114). Kick it to a worker
            // task and return immediately; the setup client polls GET /save-status for the
            // verdict (pending -> connected | failed). On success the worker reboots into
            // station mode.
            saveStatus = SaveStatus::Pending;
            xTaskCreate(saveWorkerTask, "SaveWiFi", 8192, new SaveJob{ssid, password}, 1, NULL);
            request->send(202, "text/plain", "Testing connection...");
            return true;
    }

    // Every SaveResult value is handled above (kept as a switch, not if/else, precisely so
    // -Wswitch warns if a future value is added and forgotten here) - this is an actual
    // safety net for that case, not routine defensive code: falling through with no response
    // sent leaves the client's request hanging forever instead of erroring cleanly.
    request->send(500, "text/plain", "Internal error");
    return true;
}

size_t Comms::buildCurrentStateJson(char* outBuffer, size_t outBufferSize)
{
    MissionControl& mc = MissionControl::Instance();
    const ModelConfig* runningConfig = GEOMETRY.getConfig();
    ModelId configuredId = FactoryConfig::getModelId();
    const ModelConfig* configuredConfig = getModelConfig(configuredId);
    String configuredDeviceName = DeviceIdentity::getDeviceName();
    String defaultDeviceName = DeviceIdentity::getDefaultName();

    // Snapshot under the lock rather than reading runningDeviceName directly - it can be
    // rewritten mid-copy by beginAPBroadcast() on another core (see apStateMux's comment
    // in comms.h).
    char runningNameSnapshot[sizeof(runningDeviceName)];
    portENTER_CRITICAL(&apStateMux);
    strncpy(runningNameSnapshot, runningDeviceName, sizeof(runningNameSnapshot));
    portEXIT_CRITICAL(&apStateMux);

    // Single load into a local so all 3 channels come from the same snapshot - see
    // MissionControl::liveColor()'s comment on why this is one atomic word.
    CRGB color = mc.liveColor();
    WsStateBuilder::DeviceState state{
        .power = mc.isOn(),
        .holding = mc.isHolding() || mc.isHoldPending(),
        .brightness = mc.getMaxBrightness(),
        .colorR = color.r,
        .colorG = color.g,
        .colorB = color.b,
        .colorActive = mc.isColorActive(),
        .effectName = mc.getTargetEffectName(),
        .runningModel = {static_cast<uint16_t>(runningConfig->id), runningConfig->name},
        .configuredModel = {static_cast<uint16_t>(configuredId),
                            configuredConfig ? configuredConfig->name : "Unknown"},
        .fps = PerformanceMonitor::Instance().fps(),
        .deviceUid = DeviceIdentity::getUid(),
        .runningDeviceName = runningNameSnapshot,
        .configuredDeviceName = configuredDeviceName.c_str(),
        .defaultDeviceName = defaultDeviceName.c_str(),
    };
    return WsStateBuilder::buildStateJson(state, outBuffer, outBufferSize);
}

void Comms::broadcastStateIfDirty()
{
    // Throttle to a floor interval, regardless of how fast state is being
    // marked dirty (a live colour/brightness drag does it ~100x/sec). Peek
    // the dirty flag *without* consuming it while inside the window, so the
    // latest state still goes out on the next eligible tick - the final drag
    // value is never lost, just coalesced. Unsigned subtraction is
    // rollover-safe; lastBroadcastMs == 0 means "never broadcast", always due.
    unsigned long now = nowMs();
    if (lastBroadcastMs != 0 && (now - lastBroadcastMs) < MIN_BROADCAST_INTERVAL_MS) return;

    bool dueForKeepalive =
        lastBroadcastMs != 0 && (now - lastBroadcastMs) >= STATE_KEEPALIVE_INTERVAL_MS;

    // consumeStateDirty() always runs (not short-circuited by dueForKeepalive) so a real
    // change is never left pending past this tick just because a keepalive also happened to
    // be due - the flag doesn't need to stay set once we're about to send fresh state anyway.
    bool dirty = MissionControl::Instance().consumeStateDirty();
    if (!dirty && !dueForKeepalive) return;

    char buf[WsStateBuilder::JSON_CAPACITY];
    size_t len = buildCurrentStateJson(buf, sizeof(buf));
    if (len)
    {
        ws.textAll(buf, len);
        lastBroadcastMs = now;
    }
}

void Comms::addMetricsSubscriber(uint32_t clientId)
{
    portENTER_CRITICAL(&metricsSubMux);
    // Dedupe: a re-subscribe of an already-tracked client (the page reconnecting, or a
    // duplicate subscribe message) re-arms needsFullFrame instead of consuming a second slot.
    int emptySlot = -1;
    for (size_t i = 0; i < MAX_METRICS_SUBSCRIBERS; i++)
    {
        if (metricsSubscribers[i].clientId == clientId)
        {
            metricsSubscribers[i].needsFullFrame = true;
            portEXIT_CRITICAL(&metricsSubMux);
            return;
        }
        if (emptySlot < 0 && metricsSubscribers[i].clientId == 0) emptySlot = static_cast<int>(i);
    }
    if (emptySlot >= 0) { metricsSubscribers[emptySlot] = {clientId, true}; }
    portEXIT_CRITICAL(&metricsSubMux);
    // Logged outside the critical section - Log.* isn't safe to call with interrupts disabled.
    if (emptySlot < 0)
        Log.warningln("Metrics subscriber list full (%u) - dropping subscribe from client %u",
                      static_cast<unsigned>(MAX_METRICS_SUBSCRIBERS),
                      static_cast<unsigned>(clientId));
}

void Comms::removeMetricsSubscriber(uint32_t clientId)
{
    portENTER_CRITICAL(&metricsSubMux);
    for (size_t i = 0; i < MAX_METRICS_SUBSCRIBERS; i++)
    {
        if (metricsSubscribers[i].clientId == clientId)
        {
            metricsSubscribers[i] = {};
            break;
        }
    }
    portEXIT_CRITICAL(&metricsSubMux);
}

void Comms::fillMetricsSnapshot(WsMetricsBuilder::MetricsSnapshot& out, bool includeStatic,
                                bool includeOta)
{
    // fps()/temperatureRead() NaN handling mirrors the /metrics HTTP route this replaces below -
    // buildMetricsJson() does the actual NaN->null serialization (see its own comment).
    out.uptimeMs = millis();
    out.heapFree = ESP.getFreeHeap();
    out.heapMin = ESP.getMinFreeHeap();
    out.heapTotal = ESP.getHeapSize();
    out.tempC = temperatureRead();
    out.fps = PerformanceMonitor::Instance().fps();
    out.rssi = WiFi.RSSI();
    out.currentMa = PowerMonitor::Instance().currentMa();

    out.includeStatic = includeStatic;
    if (includeStatic)
    {
        out.chip = ESP.getChipModel();
        out.cpuMhz = ESP.getCpuFreqMHz();
        out.resetReason = static_cast<int>(esp_reset_reason());
        out.version = VERSION;
        out.maxMilliamps = GEOMETRY.getConfig()->max_milliamps;
        out.brightnessCeiling = MissionControl::Instance().getBrightnessCeiling();
    }

    out.includeOta = includeOta;
    if (includeOta)
    {
        OtaUpdater::Status ota = OtaUpdater::status();
        out.updateAvailable = OtaUpdater::updateAvailable();
        // Owned copy, not a pointer into `ota` - see MetricsSnapshot::latestTag's own comment;
        // `ota` is this function's local and is gone the moment fillMetricsSnapshot() returns.
        strncpy(out.latestTag, ota.latestTag, sizeof(out.latestTag) - 1);
        out.latestTag[sizeof(out.latestTag) - 1] = '\0';
        out.otaChannel = OtaConfig::devChannel() ? "dev" : "stable";
    }
}

void Comms::pushMetricsIfDue()
{
    // Snapshot the subscriber array under the lock, clearing each needsFullFrame as it's
    // copied - then build/send entirely outside the lock (frame building calls into
    // PerformanceMonitor/WiFi/OtaUpdater/etc, none of which belong inside a spinlock that
    // disables interrupts). Same discipline as addMetricsSubscriber()/removeMetricsSubscriber().
    MetricsSubscriber snapshot[MAX_METRICS_SUBSCRIBERS];
    portENTER_CRITICAL(&metricsSubMux);
    for (size_t i = 0; i < MAX_METRICS_SUBSCRIBERS; i++)
    {
        snapshot[i] = metricsSubscribers[i];
        if (metricsSubscribers[i].clientId != 0) metricsSubscribers[i].needsFullFrame = false;
    }
    portEXIT_CRITICAL(&metricsSubMux);

    bool anySubscribers = false;
    bool anyNeedsFullFrame = false;
    for (const auto& sub : snapshot)
    {
        if (sub.clientId == 0) continue;
        anySubscribers = true;
        if (sub.needsFullFrame) anyNeedsFullFrame = true;
    }
    if (!anySubscribers) return;  // Nothing to do - skip every sensor read below entirely.

    unsigned long now = nowMs();
    bool intervalDue =
        lastMetricsPushMs == 0 || (now - lastMetricsPushMs) >= METRICS_PUSH_INTERVAL_MS;
    if (!anyNeedsFullFrame && !intervalDue) return;

    bool otaTierDue =
        lastMetricsOtaTierMs == 0 || (now - lastMetricsOtaTierMs) >= METRICS_OTA_TIER_INTERVAL_MS;

    // Full frame (static+volatile+OTA) to any subscriber that just (re)subscribed.
    if (anyNeedsFullFrame)
    {
        WsMetricsBuilder::MetricsSnapshot full{};
        fillMetricsSnapshot(full, /*includeStatic=*/true, /*includeOta=*/true);
        char buf[WsMetricsBuilder::JSON_CAPACITY];
        size_t len = WsMetricsBuilder::buildMetricsJson(full, buf, sizeof(buf));
        if (len)
        {
            for (const auto& sub : snapshot)
            {
                if (sub.clientId == 0 || !sub.needsFullFrame) continue;
                if (!ws.hasClient(sub.clientId))
                {
                    removeMetricsSubscriber(sub.clientId);
                    continue;
                }
                // A false return here just means this client's send queue is full - not the
                // same as "gone". Unsubscribing on a merely-congested client would be a
                // self-inflicted outage; it just misses this tick's frame.
                ws.text(sub.clientId, buf, len);
            }
        }
    }

    // Volatile(+OTA, when its own slower tier is due) frame to everyone else on the regular
    // interval. Deliberately *excludes* a client that just got a full frame above in this same
    // call (sub.needsFullFrame, from the pre-clear snapshot) - lastMetricsPushMs starts at 0,
    // so intervalDue is unconditionally true the moment the very first subscriber arrives,
    // which would otherwise immediately follow that subscriber's full frame with a redundant
    // volatile-only one the instant it becomes the "last frame sent" (see
    // test_metrics_subscribe_sends_one_full_frame_to_that_client_only).
    if (intervalDue)
    {
        WsMetricsBuilder::MetricsSnapshot tiered{};
        fillMetricsSnapshot(tiered, /*includeStatic=*/false, otaTierDue);
        char buf[WsMetricsBuilder::JSON_CAPACITY];
        size_t len = WsMetricsBuilder::buildMetricsJson(tiered, buf, sizeof(buf));
        if (len)
        {
            for (const auto& sub : snapshot)
            {
                if (sub.clientId == 0 || sub.needsFullFrame) continue;
                if (!ws.hasClient(sub.clientId))
                {
                    removeMetricsSubscriber(sub.clientId);
                    continue;
                }
                ws.text(sub.clientId, buf, len);
            }
        }
        lastMetricsPushMs = now;
        if (otaTierDue) lastMetricsOtaTierMs = now;
    }
}

void Comms::printWifiStatus()
{
    // Every name this device answers to (issue #135) - the boot log is the recovery path for
    // anyone with a USB cable and no other way back in, and it's what the web-installer's
    // serial console (web-installer/js/console.js) surfaces on demand via the "netinfo"
    // command (main.cpp's processSerialCommands()). Branches on AP-vs-station because the
    // "netinfo" path can land here from either state - most usefully AP mode, a freshly
    // unboxed or reset device someone is trying to find on their setup network.
    if (isInAPMode())
    {
        Log.noticeln("AP mode | SSID: %s | IP: %s | UID: %s | mDNS: %s",
                     DeviceIdentity::getDeviceName().c_str(), WiFi.softAPIP().toString().c_str(),
                     DeviceIdentity::getUid(), mdnsHostsJson().c_str());
        return;
    }

    Log.noticeln("Connected to %s | IP: %s | UID: %s | mDNS: %s", WiFi.SSID().c_str(),
                 WiFi.localIP().toString().c_str(), DeviceIdentity::getUid(),
                 mdnsHostsJson().c_str());
}