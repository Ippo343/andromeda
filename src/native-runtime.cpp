#include "native-runtime.h"

#include <ArduinoLog.h>
#include <FastLED.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <deque>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#include "animation-base.h"
#include "device-identity.h"
#include "geometry/geometry.h"
#include "geometry/model_registry.h"
#include "mission-control.h"
#include "perf-monitor.h"
#include "ws-command-parser.h"
#include "ws-state-builder.h"

namespace NativeRuntime
{

namespace
{
ModelId resolvedModel = ModelId::SINGLE_STRIP_TEST_DEVICE;

bool namesMatchCaseInsensitive(const char* a, const char* b)
{
    while (*a != '\0' && *b != '\0')
    {
        char ca = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? static_cast<char>(*b + 32) : *b;
        if (ca != cb) return false;
        a++;
        b++;
    }
    return *a == *b;
}

// --- stdin -> command dispatch -----------------------------------------
//
// A dedicated reader thread does the blocking std::getline() read (Windows
// has no simple portable way to poll a piped stdin without blocking); it
// only ever touches this small, self-contained mutex-protected queue. The
// main loop thread (the only thread that ever calls into MissionControl)
// drains it once per tick in pumpStdinCommands() - MissionControl's own
// command queue (test/mocks/freertos/queue.h's SimpleQueue) is never
// touched from more than one thread, so its lack of internal locking is
// unaffected by this.
std::mutex stdinMutex;
std::deque<std::string> pendingLines;
std::atomic<bool> stdinThreadStarted{false};

void stdinReaderLoop()
{
    std::string line;
    while (std::getline(std::cin, line))
    {
        std::lock_guard<std::mutex> lock(stdinMutex);
        pendingLines.push_back(std::move(line));
    }
    // EOF (bridge closed its pipe, or the process is being killed): nothing
    // more to read, just let the thread end.
}

void ensureStdinThreadStarted()
{
    if (stdinThreadStarted.exchange(true)) return;
    std::thread(stdinReaderLoop).detach();
}

// Mirrors comms.cpp's WS_EVT_DATA handler dispatch exactly (same parser,
// same brightness fast-path, same live-color fast-path) - see comms.cpp.
void processIncomingLine(const std::string& line)
{
    MissionControl& mc = MissionControl::Instance();

    uint8_t brightnessValue;
    bool brightnessCommit;
    Command command;
    if (WsCommandParser::parseBrightness(line.c_str(), brightnessValue, brightnessCommit))
    {
        mc.setMaxBrightness(brightnessValue);
        if (brightnessCommit) BrightnessConfig::persist(brightnessValue);
    }
    else if (WsCommandParser::parse(line.c_str(), command))
    {
        if (command.type == CommandType::COLOR && mc.isColorActive())
            mc.setLiveColor(command.r, command.g, command.b);
        else
            mc.queueWebCommand(command);
    }
    else { Log.warningln("Unrecognized stdin command line: %s", line.c_str()); }
}

void pumpStdinCommands()
{
    ensureStdinThreadStarted();

    std::deque<std::string> lines;
    {
        std::lock_guard<std::mutex> lock(stdinMutex);
        lines.swap(pendingLines);
    }
    for (const std::string& line : lines) processIncomingLine(line);
}

// --- state/frame/geometry -> stdout --------------------------------------

void writeLine(const char* data, size_t len)
{
    std::fwrite(data, 1, len, stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

// Mirrors Comms::buildCurrentStateJson() (comms.cpp): builds the same
// WsStateBuilder::DeviceState from MissionControl/GEOMETRY/FactoryConfig/
// PerformanceMonitor/DeviceIdentity. There's no separate "running" vs.
// "configured" device name here (no AP/station boot-time capture like
// Comms::startAPMode() does) - both fields just read the live name, which
// only matters for the wire format's nameRebootRequired flag, harmless to
// always report false natively.
void emitStateIfDirty()
{
    MissionControl& mc = MissionControl::Instance();
    if (!mc.consumeStateDirty()) return;

    const ModelConfig* runningConfig = GEOMETRY.getConfig();
    ModelId configuredId = FactoryConfig::getModelId();
    const ModelConfig* configuredConfig = getModelConfig(configuredId);
    String deviceName = DeviceIdentity::getDeviceName();

    WsStateBuilder::DeviceState state{
        .power = mc.isOn(),
        .holding = mc.isHolding() || mc.isHoldPending(),
        .brightness = mc.getMaxBrightness(),
        .colorR = mc.staticColor.r,
        .colorG = mc.staticColor.g,
        .colorB = mc.staticColor.b,
        .colorActive = mc.isColorActive(),
        .effectName = mc.getEffectName(),
        .runningModel = {static_cast<uint16_t>(runningConfig->id), runningConfig->name},
        .configuredModel = {static_cast<uint16_t>(configuredId),
                            configuredConfig ? configuredConfig->name : "Unknown"},
        .fps = PerformanceMonitor::Instance().fps(),
        .deviceUid = DeviceIdentity::getUid(),
        .runningDeviceName = deviceName.c_str(),
        .configuredDeviceName = deviceName.c_str(),
    };

    char buf[WsStateBuilder::JSON_CAPACITY];
    size_t len = WsStateBuilder::buildStateJson(state, buf, sizeof(buf));
    if (len) writeLine(buf, len);
}

// Reused across calls (no per-frame heap allocation once warmed up) - hand-
// rolled serialization rather than a full JSON library, since this runs
// once per real FASTLED_SHOW() tick, same spirit as WsCommandParser's
// hand-rolled parsing.
std::string frameBuffer;

void frameCaptureHook()
{
    static bool reserved = false;
    if (!reserved)
    {
        // libstdc++'s default-constructed std::string already has a nonzero
        // small-string-optimization capacity, so a capacity()==0 check never
        // fires here - use an explicit flag instead to actually get the
        // one-time sized reserve() this comment promises.
        size_t totalLeds = 0;
        for (size_t s = 0; s < GEOMETRY.getNumStrips(); s++)
            totalLeds += GEOMETRY.getStrip(s).num_leds;
        frameBuffer.reserve(totalLeds * 15 + 64);  // "[255,255,255]," <= 15 bytes/LED, plus slack
        reserved = true;
    }

    frameBuffer.clear();
    frameBuffer += "{\"type\":\"frame\",\"leds\":[";

    bool first = true;
    char numbuf[16];
    for (size_t s = 0; s < GEOMETRY.getNumStrips(); s++)
    {
        LedStrip& strip = GEOMETRY.getStrip(s);
        for (size_t i = 0; i < strip.num_leds; i++)
        {
            if (!first) frameBuffer += ',';
            first = false;
            CRGB c = strip.buffer[i];
            int n = std::snprintf(numbuf, sizeof(numbuf), "[%u,%u,%u]", c.r, c.g, c.b);
            frameBuffer.append(numbuf, static_cast<size_t>(n));
        }
    }
    frameBuffer += "]}";
    writeLine(frameBuffer.data(), frameBuffer.size());
}

// Sent once, right after boot: at this point no effect has run yet, so
// GEOMETRY's strips still hold their original, untransformed coordinates
// (some effects call GEOMETRY.applyGlobalRandomRotation() later, which
// mutates them in place for the effect's own math - the visualizer wants
// the LEDs' real fixed physical positions, not that transform, so this
// deliberately reads them exactly once, before anything can rotate them).
void emitGeometryOnce()
{
    const ModelConfig* config = GEOMETRY.getConfig();
    size_t numStrips = GEOMETRY.getNumStrips();

    // Runs once at boot, not the hot path frameCaptureHook()'s hand-rolled
    // serialization is optimized for - reuse ArduinoJson instead (already a
    // project dependency, already the pattern ws-state-builder.h uses for
    // the identical job), which gets a real overflow guard for free instead
    // of unchecked string concatenation. Capacity is computed precisely
    // from the actual strip/LED counts rather than a guessed constant,
    // since that varies a lot per model (L70 MK1 alone has 267 LEDs).
    size_t capacity = JSON_OBJECT_SIZE(4) + JSON_ARRAY_SIZE(numStrips) + 128;
    for (size_t s = 0; s < numStrips; s++)
    {
        size_t n = GEOMETRY.getStrip(s).num_leds;
        capacity += JSON_OBJECT_SIZE(2) + JSON_ARRAY_SIZE(n) + n * JSON_ARRAY_SIZE(2);
    }

    DynamicJsonDocument doc(capacity);
    doc["type"] = "geometry";
    doc["width_mm"] = config->screen_width_mm;
    doc["height_mm"] = config->screen_height_mm;
    JsonArray strips = doc.createNestedArray("strips");
    for (size_t s = 0; s < numStrips; s++)
    {
        LedStrip& strip = GEOMETRY.getStrip(s);
        JsonObject stripObj = strips.createNestedObject();
        stripObj["num_leds"] = strip.num_leds;
        JsonArray points = stripObj.createNestedArray("points");
        for (size_t i = 0; i < strip.num_leds; i++)
        {
            JsonArray point = points.createNestedArray();
            point.add(strip.leds[i].cartesian.x);
            point.add(strip.leds[i].cartesian.y);
        }
    }

    std::string buf;
    serializeJson(doc, buf);
    writeLine(buf.data(), buf.size());
}

}  // namespace

void init(int argc, char** argv)
{
    // Lets callers (tools/native-bridge/run-simulator.ps1's model dropdown)
    // read the registry's actual model names off the freshly-built binary
    // instead of hardcoding a list that can drift from src/geometry/*.cpp.
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--list-models") != 0) continue;
        for (size_t m = 0; m < NUM_MODELS; m++) std::puts(MODEL_REGISTRY[m]->name);
        std::exit(0);
    }

    static const char* PREFIX = "--model=";
    const size_t prefixLen = strlen(PREFIX);

    for (int i = 1; i < argc; i++)
    {
        if (strncmp(argv[i], PREFIX, prefixLen) != 0) continue;

        const char* requestedName = argv[i] + prefixLen;
        for (size_t m = 0; m < NUM_MODELS; m++)
        {
            if (namesMatchCaseInsensitive(MODEL_REGISTRY[m]->name, requestedName))
            {
                resolvedModel = MODEL_REGISTRY[m]->id;
                Log.noticeln("Native runtime: selected model '%s' via --model arg",
                             MODEL_REGISTRY[m]->name);
                return;
            }
        }

        Log.warningln("Native runtime: unknown --model value '%s', falling back to default",
                      requestedName);
        return;
    }

    Log.noticeln("Native runtime: no --model arg given, defaulting to '%s'",
                 getModelConfig(resolvedModel)->name);
}

ModelId model() { return resolvedModel; }

void installProtocol()
{
    // MinGW's CRT leaves stdout effectively unbuffered when it's a pipe (not
    // a console) on Windows, splitting each fwrite() of a multi-KB frame
    // line into many small WriteFile() syscalls - each one individually
    // cheap, but their sheer number was the entire native-vs-bridge FPS gap
    // (measured: ~37fps -> ~57fps on Andromeda Mk1's 161-LED frame line just
    // from this one change). Forcing a real block buffer makes each
    // writeLine() a single memcpy-then-one-syscall instead.
    static char stdoutIoBuf[1 << 16];
    std::setvbuf(stdout, stdoutIoBuf, _IOFBF, sizeof(stdoutIoBuf));

    g_frameCaptureHook = frameCaptureHook;
    emitGeometryOnce();
}

void tick()
{
    pumpStdinCommands();
    emitStateIfDirty();
    // Deliberately no frame-rate pacing here: min_frame_duration_ms exists
    // to save power on battery-powered LED hardware, which doesn't apply
    // running on a dev machine - the native loop just runs as fast as the
    // host CPU (and the stdio pipe to the bridge) allow.
}

}  // namespace NativeRuntime
