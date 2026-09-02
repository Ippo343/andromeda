#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "LittleFS.h"
#include "WString.h"

// Minimal stand-in for the lacamera/ESPAsyncWebServer library, scoped to
// what comms.cpp actually uses: registering routes/a WebSocket handler and
// serving a response. Real request dispatch/networking is out of scope -
// native tests construct an AsyncWebServerRequest directly and invoke a
// registered handler themselves (see test_comms_integration.cpp).

using WebRequestMethod = int;
constexpr WebRequestMethod HTTP_GET = 1;
constexpr WebRequestMethod HTTP_POST = 2;

// Minimal stand-in for the real library's AsyncWebHeader - just enough to match
// AsyncWebServerRequest::getHeader()'s return shape (a pointer with ->value()).
class AsyncWebHeader
{
   public:
    explicit AsyncWebHeader(const String& value) : value_(value) {}
    const String& value() const { return value_; }

   private:
    String value_;
};

class AsyncWebServerRequest
{
   public:
    // --- Test setup ---
    void setArg(const char* name, const String& value) { args[name] = value; }
    void setHost(const String& h) { hostValue = h; }
    void setHeader(const char* name, const String& value)
    {
        headers[name] = std::unique_ptr<AsyncWebHeader>(new AsyncWebHeader(value));
    }

    // --- Production API (called by route handlers) ---
    String arg(const char* name) const
    {
        auto it = args.find(name);
        return it != args.end() ? it->second : String("");
    }
    String host() const { return hostValue; }
    bool hasHeader(const char* name) const { return headers.find(name) != headers.end(); }
    // Real signature returns const AsyncWebHeader* (nullptr if absent) - callers must
    // check hasHeader() first, same as the real library's own header comment.
    const AsyncWebHeader* getHeader(const char* name) const
    {
        auto it = headers.find(name);
        return it != headers.end() ? it->second.get() : nullptr;
    }

    void send(int code, const String& contentType, const String& content = String())
    {
        responseCode = code;
        responseContentType = contentType;
        responseBody = content;
    }
    template <typename Fs>
    void send(Fs&, const String& path, const String& contentType)
    {
        responseCode = 200;
        responseContentType = contentType;
        responseBody = String("<file:") + path + String(">");
    }
    void redirect(const String& url)
    {
        wasRedirected = true;
        redirectedTo = url;
    }

    // --- Test observation ---
    int responseCode = 0;
    String responseContentType;
    String responseBody;
    bool wasRedirected = false;
    String redirectedTo;

   private:
    std::map<std::string, String> args;
    std::map<std::string, std::unique_ptr<AsyncWebHeader>> headers;
    String hostValue;
};

using ArRequestHandlerFunction = std::function<void(AsyncWebServerRequest*)>;

enum AwsEventType
{
    WS_EVT_CONNECT,
    WS_EVT_DISCONNECT,
    WS_EVT_DATA,
    WS_EVT_ERROR,
};

enum AwsFrameType
{
    WS_TEXT,
    WS_BINARY,
};

struct AwsFrameInfo
{
    bool final = true;
    size_t index = 0;
    size_t len = 0;
    AwsFrameType opcode = WS_TEXT;
};

class AsyncWebSocketClient
{
   public:
    // Test helper: captures what the production code sent via client->text(...).
    std::string lastSentText;
    void text(const char* msg, size_t len) { lastSentText.assign(msg, len); }

    // Test helper: captures the auto-ping period the production code configured.
    uint16_t lastKeepAlivePeriodSeconds = 0;
    void keepAlivePeriod(uint16_t seconds) { lastKeepAlivePeriodSeconds = seconds; }

    // Test helper: records whether the production code closed this client
    // (e.g. rejecting a cross-origin WebSocket handshake).
    bool wasClosed = false;
    void close() { wasClosed = true; }
};

using AwsEventHandler = std::function<void(class AsyncWebSocket*, AsyncWebSocketClient*,
                                           AwsEventType, void*, uint8_t*, size_t)>;

class AsyncWebSocket
{
   public:
    explicit AsyncWebSocket(const char*) {}

    void onEvent(AwsEventHandler handler) { eventHandler = handler; }

    // Test helper: simulate a WS_EVT_DATA frame arriving.
    void simulateDataFrame(AwsFrameInfo& info, uint8_t* data, size_t len)
    {
        if (eventHandler) eventHandler(this, nullptr, WS_EVT_DATA, &info, data, len);
    }

    // Test helper: simulate a new client connecting. The real library passes
    // the HTTP upgrade request as the event `arg` for WS_EVT_CONNECT, so the
    // overload taking a request lets tests exercise the origin check.
    void simulateConnect(AsyncWebSocketClient* client)
    {
        if (eventHandler) eventHandler(this, client, WS_EVT_CONNECT, nullptr, nullptr, 0);
    }
    void simulateConnect(AsyncWebSocketClient* client, AsyncWebServerRequest* request)
    {
        if (eventHandler) eventHandler(this, client, WS_EVT_CONNECT, request, nullptr, 0);
    }

    // Test helper: captures what the production code broadcast via textAll(...).
    std::string lastBroadcastText;
    void textAll(const char* msg, size_t len) { lastBroadcastText.assign(msg, len); }

    // Test helper: records the cap comms.cpp asked for, and how many times it asked -
    // there's no simulated client list here to actually prune.
    uint16_t lastCleanupMaxClients = 0;
    unsigned cleanupClientsCallCount = 0;
    void cleanupClients(uint16_t maxClients = 4)
    {
        lastCleanupMaxClients = maxClients;
        cleanupClientsCallCount++;
    }

    AwsEventHandler eventHandler;
};

class AsyncWebServer
{
   public:
    explicit AsyncWebServer(int /*port*/) {}

    void on(const char* uri, WebRequestMethod method, ArRequestHandlerFunction handler)
    {
        handlers[{uri, method}] = handler;
    }
    void addHandler(AsyncWebSocket* ws) { webSocket = ws; }
    void onNotFound(ArRequestHandlerFunction handler) { notFoundHandler = handler; }
    void begin() {}

    // Test helper: look up a registered route's handler directly.
    ArRequestHandlerFunction* findHandler(const char* uri, WebRequestMethod method)
    {
        auto it = handlers.find({uri, method});
        return it != handlers.end() ? &it->second : nullptr;
    }

    AsyncWebSocket* webSocket = nullptr;
    ArRequestHandlerFunction notFoundHandler;

   private:
    std::map<std::pair<std::string, WebRequestMethod>, ArRequestHandlerFunction> handlers;
};
