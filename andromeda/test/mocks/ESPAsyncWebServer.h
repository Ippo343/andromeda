#pragma once

#include <functional>
#include <map>
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

class AsyncWebServerRequest
{
   public:
    // --- Test setup ---
    void setArg(const char* name, const String& value) { args[name] = value; }
    void setHost(const String& h) { hostValue = h; }

    // --- Production API (called by route handlers) ---
    String arg(const char* name) const
    {
        auto it = args.find(name);
        return it != args.end() ? it->second : String("");
    }
    String host() const { return hostValue; }

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
