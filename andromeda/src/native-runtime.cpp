#include "native-runtime.h"

#include <ArduinoLog.h>
#include <FastLED.h>

#include <atomic>
#include <chrono>
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

// AbstractBlockingAnimation::GetName()/run() are declared in
// animation-base.h but never defined anywhere in production code - every
// real animation overrides both, so this dead code path never needed a
// definition until now. main.cpp's NATIVE_RUNTIME branch never instantiates
// AbstractBlockingAnimation (or its indicator subclasses) directly, but
// MinGW's linker still needs the base class's vtable key function defined
// somewhere in the link - see the identical stand-in in
// test_mission_control.cpp/test_animations.cpp/test_comms_integration.cpp.
const char* AbstractBlockingAnimation::GetName() { return "AbstractBlockingAnimation"; }
void AbstractBlockingAnimation::run() {}

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
    ModelId configuredId =
        FactoryConfig::isConfigured() ? FactoryConfig::getModelId() : runningConfig->id;
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
    if (frameBuffer.capacity() == 0)
    {
        size_t totalLeds = 0;
        for (size_t s = 0; s < GEOMETRY.getNumStrips(); s++)
            totalLeds += GEOMETRY.getStrip(s).num_leds;
        frameBuffer.reserve(totalLeds * 15 + 64);  // "[255,255,255]," <= 15 bytes/LED, plus slack
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

    std::string buf;
    buf += "{\"type\":\"geometry\",\"width_mm\":";
    buf += std::to_string(config->screen_width_mm);
    buf += ",\"height_mm\":";
    buf += std::to_string(config->screen_height_mm);
    buf += ",\"strips\":[";

    for (size_t s = 0; s < GEOMETRY.getNumStrips(); s++)
    {
        if (s > 0) buf += ',';
        LedStrip& strip = GEOMETRY.getStrip(s);
        buf += "{\"num_leds\":";
        buf += std::to_string(strip.num_leds);
        buf += ",\"points\":[";
        for (size_t i = 0; i < strip.num_leds; i++)
        {
            if (i > 0) buf += ',';
            buf += '[';
            buf += std::to_string(strip.leds[i].cartesian.x);
            buf += ',';
            buf += std::to_string(strip.leds[i].cartesian.y);
            buf += ']';
        }
        buf += "]}";
    }
    buf += "]}";
    writeLine(buf.data(), buf.size());
}

}  // namespace

void init(int argc, char** argv)
{
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
    g_frameCaptureHook = frameCaptureHook;
    emitGeometryOnce();
}

void tick(milliseconds_t frameDurationCapMs)
{
    pumpStdinCommands();
    emitStateIfDirty();

    // 0 means "uncapped" on real hardware; an actually-uncapped native loop
    // would just spin a full host core for no visual benefit, so fall back
    // to a sane default instead of honoring 0 literally.
    milliseconds_t targetMs = frameDurationCapMs > 0 ? frameDurationCapMs : 16;

    static auto lastTick = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTick).count();
    if (elapsedMs < static_cast<long long>(targetMs))
        std::this_thread::sleep_for(std::chrono::milliseconds(targetMs - elapsedMs));
    lastTick = std::chrono::steady_clock::now();
}

}  // namespace NativeRuntime
