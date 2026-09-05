#pragma once

#include <ArduinoJson.h>

// Pure, dependency-light builder for the "metrics" JSON message pushed to
// WebSocket clients subscribed to that topic (see Comms::pushMetricsIfDue()),
// and reused by the plain HTTP /metrics route so the two can't drift apart.
// Only depends on ArduinoJson (no Arduino String, no WiFi/networking), so
// it's natively unit-testable - sibling to ws-state-builder.h, deliberately
// kept separate from it (see that header's own comment): the "state"
// broadcast goes to every connected client at up to 10Hz during a live
// colour/brightness drag, while this one is per-client opt-in and tiered
// (#214) - folding metrics into it would push a much bigger payload to
// clients (e.g. the Controls page) that never asked for it.

namespace WsMetricsBuilder
{

// Plain snapshot of everything a metrics frame might carry. Built by Comms
// from PerformanceMonitor/PowerMonitor/OtaUpdater/etc; kept as a POD so this
// header has no Arduino/singleton dependencies of its own. `tempC`/`fps` use
// NaN to mean "no reading available" (mirrors the /metrics HTTP handler),
// serialized as JSON null.
struct MetricsSnapshot
{
    // --- Volatile tier: sent every push, present in every frame. ---
    unsigned long uptimeMs;
    uint32_t heapFree;
    uint32_t heapMin;
    uint32_t heapTotal;
    float tempC;
    float fps;
    int rssi;
    uint32_t currentMa;

    // --- Static tier: boot-constant, sent only when includeStatic is set
    // (the full frame on subscribe). ---
    bool includeStatic;
    const char* chip;
    uint32_t cpuMhz;
    int resetReason;
    const char* version;
    uint32_t maxMilliamps;
    uint8_t brightnessCeiling;

    // --- OTA tier: changes rarely, sent only when includeOta is set (the
    // full frame, or the periodic slow-tier push). ---
    bool includeOta;
    bool updateAvailable;
    // Owned buffer, not a const char* like the other string fields here: the
    // caller fills this from OtaUpdater::Status::latestTag, which lives in a
    // locally-constructed Status the caller's OtaUpdater::status() call
    // returns by value - a pointer into that would dangle the moment the
    // caller's own stack frame that made the call unwinds. Sized to match
    // OtaUpdater::Status::latestTag (ota-updater.h) so a straight strncpy
    // copy never truncates a real tag.
    char latestTag[48];
    const char* otaChannel;
};

// StaticJsonDocument capacity for the schema below. Fixed, zero-heap - bump
// this if fields are added. Measured via StaticJsonDocument::memoryUsage()
// on this toolchain: the full (static+volatile+OTA) frame's 18 keys (#237
// added brightnessCeiling) with a worst-case 47-char latestTag (ArduinoJson
// copies a mutable char[] like MetricsSnapshot::latestTag into its own pool,
// unlike the const char* fields here, which it stores by reference - see
// that field's own comment) need ~592 bytes; 768 keeps real headroom above
// that rather than sitting right at the edge. NOTE: buf[JSON_CAPACITY] plus a
// StaticJsonDocument<JSON_CAPACITY> both land on the caller's stack, entirely
// separate from ws-state-builder.h's own buf[JSON_CAPACITY=4096] +
// StaticJsonDocument<4096> (the two are never nested - see
// Comms::pushMetricsIfDue() and Comms::broadcastStateIfDirty(), which each
// build and send their own buffer in their own call frame). Keep
// 2*JSON_CAPACITY well under the 16384-byte task stacks either can run on.
constexpr size_t JSON_CAPACITY = 768;

// Serializes `snapshot` into `outBuffer` (of size `outBufferSize`). Returns
// the number of bytes written (0 on failure, e.g. if the document overflowed
// its fixed capacity), mirroring ArduinoJson's own
// serializeJson(doc, char*, size_t) return convention and
// WsStateBuilder::buildStateJson()'s.
inline size_t buildMetricsJson(const MetricsSnapshot& snapshot, char* outBuffer,
                               size_t outBufferSize)
{
    StaticJsonDocument<JSON_CAPACITY> doc;

    doc["type"] = "metrics";

    doc["uptimeMs"] = snapshot.uptimeMs;
    doc["heapFree"] = snapshot.heapFree;
    doc["heapMin"] = snapshot.heapMin;
    doc["heapTotal"] = snapshot.heapTotal;
    // ArduinoJson serializes NaN as JSON null when asked to via a float; an
    // explicit branch keeps that behaviour obvious and matches the /metrics
    // HTTP handler's own NaN self-comparison guard.
    if (snapshot.tempC == snapshot.tempC)
        doc["tempC"] = snapshot.tempC;
    else
        doc["tempC"] = nullptr;
    if (snapshot.fps == snapshot.fps)
        doc["fps"] = snapshot.fps;
    else
        doc["fps"] = nullptr;
    doc["rssi"] = snapshot.rssi;
    doc["currentMa"] = snapshot.currentMa;

    if (snapshot.includeStatic)
    {
        doc["chip"] = snapshot.chip;
        doc["cpuMhz"] = snapshot.cpuMhz;
        doc["resetReason"] = snapshot.resetReason;
        doc["version"] = snapshot.version;
        doc["maxMilliamps"] = snapshot.maxMilliamps;
        doc["brightnessCeiling"] = snapshot.brightnessCeiling;
    }

    if (snapshot.includeOta)
    {
        doc["updateAvailable"] = snapshot.updateAvailable;
        doc["latestTag"] = snapshot.latestTag;
        doc["otaChannel"] = snapshot.otaChannel;
    }

    if (doc.overflowed()) return 0;
    return serializeJson(doc, outBuffer, outBufferSize);
}

}  // namespace WsMetricsBuilder
