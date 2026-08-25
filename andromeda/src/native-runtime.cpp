#include "native-runtime.h"

#include <ArduinoLog.h>
#include <FastLED.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include "animation-base.h"
#include "geometry/geometry.h"
#include "geometry/model_registry.h"

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

void tick(milliseconds_t frameDurationCapMs)
{
    static auto lastTick = std::chrono::steady_clock::now();
    static auto lastDebugPrint = lastTick;
    static uint32_t frameCount = 0;

    frameCount++;
    auto now = std::chrono::steady_clock::now();

    if (now - lastDebugPrint >= std::chrono::seconds(1))
    {
        if (GEOMETRY.getNumStrips() > 0 && GEOMETRY.getStrip(0).num_leds > 0)
        {
            CRGB c = GEOMETRY.getStrip(0).buffer[0];
            std::printf("[native-runtime] %u fps, model=%s, strips=%zu, strip0 led0=(%u,%u,%u)\n",
                        frameCount, GEOMETRY.getConfig()->name, GEOMETRY.getNumStrips(), c.r, c.g,
                        c.b);
            std::fflush(stdout);
        }
        frameCount = 0;
        lastDebugPrint = now;
    }

    // 0 means "uncapped" on real hardware; an actually-uncapped native loop
    // would just spin a full host core for no visual benefit, so fall back
    // to a sane default instead of honoring 0 literally.
    milliseconds_t targetMs = frameDurationCapMs > 0 ? frameDurationCapMs : 16;

    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTick).count();
    if (elapsedMs < static_cast<long long>(targetMs))
        std::this_thread::sleep_for(std::chrono::milliseconds(targetMs - elapsedMs));

    lastTick = std::chrono::steady_clock::now();
}

}  // namespace NativeRuntime
