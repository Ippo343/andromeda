#include <LittleFS.h>
#include <esp_system.h>  // esp_reset_reason()

#include "animations.h"
#include "comms.h"
#include "effects.h"
#include "energy-param.h"
#include "geometry/geometry.h"
#ifndef NATIVE_RUNTIME
// Real Serial/file logging via ArduinoLog's Print API - meaningless on the
// host, where Log is instead the no-op LoggingStub mock (test/mocks/
// ArduinoLog.h) that every Log.*ln() call below already degrades to.
#include "loggers.h"
#endif
#include "mission-control.h"
#include "perf-monitor.h"
#include "version.h"

#ifndef NATIVE_RUNTIME
#include "status-led.h"
#endif

#ifdef NATIVE_RUNTIME
#include "native-runtime.h"
#endif

// Brightness clamped to on a brownout-reset boot - low enough to plausibly fit under any
// PSU this device could ship with, breaking a brightness-induced brownout boot loop.
constexpr uint8_t BROWNOUT_SAFE_BRIGHTNESS = 32;

void setup()
{
#ifndef NATIVE_RUNTIME
    // Before anything else - including Serial - so a board that hangs in early
    // init still visibly shows it has power.
    statusLedOn();

    // No wait for Serial to become ready: the device normally runs on battery/mains with no
    // monitor attached, and that has to be the fast path, not the one that pays a timeout.
    // Serial.begin() + the CDC driver buffer early output regardless, so a monitor attached
    // shortly after boot still catches the startup log.
    Serial.begin(115200);

    // Wait a little bit to allow the PC to start the serial monitor before we start spamming it
    // with logs
    delay(100);

    LittleFS.begin();
    setupLoggers();
#endif
    Log.noticeln("=== Andromeda Device Starting Up ===");
    Log.noticeln("=== Version: %s", VERSION);
    Log.noticeln("====================================");

    // Load model ID
    ModelId model;
#ifdef NATIVE_RUNTIME
    // No factory NVS on the host - resolved once from argv by
    // NativeRuntime::init(), called before setup() in main() below.
    model = NativeRuntime::model();
#else
    if (FactoryConfig::isConfigured())
    {
        model = FactoryConfig::getModelId();
        Log.noticeln("Loading factory configuration: %s", getModelName(model));
    }
    else
    {
        model = ModelId::SINGLE_STRIP_TEST_DEVICE;
        Log.warningln("Device not factory configured, using default: %s", getModelName(model));
    }
#endif

#ifdef NATIVE_RUNTIME
    // Skips bindHardwareDrivers() (the FastLED.addLeds<...> calls) - there's
    // no real/simulated LED hardware to bind to on the host.
    GEOMETRY.initializeForTest(model);
    // Must run right after geometry loads and before anything can rotate it
    // (see native-runtime.cpp's emitGeometryOnce() for why that ordering
    // matters) - installs the frame-capture hook and emits the one-time
    // geometry message for the visualizer bridge.
    NativeRuntime::installProtocol();
#else
    GEOMETRY.initialize(model);
#endif

    // Log device configuration
    const ModelConfig* config = GEOMETRY.getConfig();
    Log.noticeln("Device: %s", config->name);

    FastLED.setCorrection(TypicalLEDStrip);

    // Global safety net on top of (not instead of) the user-facing brightness slider:
    // FastLED estimates the actual per-frame current draw from the rendered colors and
    // dims globally to stay under this budget, so a customer sliding to 255 and picking
    // white can't pull more current than the rail is rated for. See ModelConfig's
    // max_milliamps for why this is a placeholder value pending real PSU specs.
    FastLED.setMaxPowerInVoltsAndMilliamps(5, config->max_milliamps);

    seedRNGs();

    // Restore the last brightness configured via the web UI before running
    // any boot-status indicator below. WiFiConnectingAnimation/
    // WiFiSuccessAnimation/ErrorAnimation call FASTLED_SHOW() directly
    // rather than going through MissionControl::update() (which hasn't
    // started yet), so without this they always render at FastLED's
    // power-on default (full brightness) regardless of what's configured -
    // the very first thing the device ever lights up ignored the stored
    // value. 64 is the fallback for a never-configured device, to avoid
    // burning my eyes while working at the Social Hub's desk.
    uint8_t maxBrightness = BrightnessConfig::load(64);

    // A PSU that sags and browns out at high brightness resets the chip and, without
    // this, comes right back up at the exact same brightness - a hard boot loop with no
    // escape and no on-device diagnosis. Clamp to a safe low value for this one boot
    // instead of trusting the stored value; nothing is lost from NVS, so the next
    // deliberate brightness change (or a clean boot) restores normal behavior.
    if (esp_reset_reason() == ESP_RST_BROWNOUT)
    {
        Log.warningln(
            "Booted after a brownout reset - clamping brightness to %d to break "
            "a possible boot loop",
            BROWNOUT_SAFE_BRIGHTNESS);
        if (maxBrightness > BROWNOUT_SAFE_BRIGHTNESS) maxBrightness = BROWNOUT_SAFE_BRIGHTNESS;
    }

    MissionControl::Instance().setMaxBrightness(maxBrightness);
    FastLED.setBrightness(dim8_raw(maxBrightness));

    // Restores power state and, if the user explicitly picked one, the held effect/color -
    // so the device comes back exactly as it was left instead of always starting in random
    // rotation. Safe here, before the render loop's first update() tick: it only sets
    // MissionControl's in-memory state via the same paths a live command would use.
    MissionControl::Instance().restoreStartupState();

#ifndef NATIVE_RUNTIME
    WiFiConnectingAnimation connecting;
    connecting.run();

    switch (Comms::Instance().setup())
    {
        case Comms::SetupOutcome::Connected:
        {
            WiFiSuccessAnimation success;
            success.run();
            break;
        }
        case Comms::SetupOutcome::ConnectFailed:
        {
            // Stored credentials exist but didn't work (router password changed, out of
            // range, ...) - genuinely something to flag, unlike NeverConfigured below.
            ErrorAnimation error;
            error.run();
            break;
        }
        case Comms::SetupOutcome::NeverConfigured:
            // A brand-new device's expected first-boot state - landing in AP mode here is
            // normal, not an error, so no alarming indicator for what is otherwise a
            // completely ordinary out-of-box power-on.
            break;
    }
#endif

#if defined(ESP32_C3)
    // The C3 sits enclosed in the small L10 case with little room to shed
    // heat, and 80MHz is plenty for the L10's effects - dropping to it here
    // saves power/thermal headroom on that one board.
    setCpuFrequencyMhz(80);
#endif
    MissionControl::Instance().setFrameDurationCap(config->min_frame_duration_ms);

#ifndef NATIVE_RUNTIME
    // Nothing previously recovered from a hang inside an effect, FASTLED_SHOW(), or
    // MissionControl::update() - the device would just sit frozen forever. The framework's
    // loopTask() already resets the TWDT for us once we're subscribed (esp32-hal-misc.c),
    // and the SDK default (5s, panic-on-trigger - see sdkconfig CONFIG_ESP_TASK_WDT_*)
    // reboots the device instead. Enabled last, once setup() itself can no longer trip it.
    enableLoopWDT();
#endif
}

#ifdef NATIVE_RUNTIME
void loop()
{
    MissionControl::Instance().update(millis());
    NativeRuntime::tick();
}

int main(int argc, char** argv)
{
    NativeRuntime::init(argc, argv);
    setup();
    while (true) loop();
    return 0;
}
#else
void loop() { MissionControl::Instance().update(millis()); }
#endif
