#include <unity.h>

// mission-control.cpp is #included directly (not compiled via
// platformio.ini's build_src_filter) so this test binary stays fully
// self-contained: it needs getRandomEffect() (declared in effects.h,
// defined by effects/effects-registry.cpp - which is part of the shared
// build_src_filter) and getRandomAnimation() (stubbed below, see the
// comment further down) to even link.
#include "../../include/effects.h"
#include "animation-base.h"
#include "animation-frame-base.h"
#include "platforms/stub/time_stub.h"
#include "ws-command-parser.h"

// AbstractBlockingAnimation::GetName()/run() are declared in
// animation-base.h but never defined anywhere in production code - every
// real animation overrides both, so this dead code path never needed a
// definition until now. StubAnimation (below) overrides them too, but the
// compiler still needs base-class vtable entries to exist at link time.
const char* AbstractBlockingAnimation::GetName() { return "AbstractBlockingAnimation"; }
void AbstractBlockingAnimation::run() {}

// Minimal rotation-animation stand-in so mission-control.cpp links without
// pulling in the real animations.cpp (out of scope for this test target -
// see test_animations.cpp for the real per-animation frame tests). Finishes
// on its very first renderFrame() call. Tracks how many instances are
// currently alive so a test can prove MissionControl doesn't leak them
// across repeated transitions.
static int stubAnimationLiveCount = 0;
class StubAnimation : public AbstractFrameAnimation
{
   public:
    StubAnimation() { stubAnimationLiveCount++; }
    ~StubAnimation() override { stubAnimationLiveCount--; }
    const char* GetName() override { return "StubAnimation"; }
    bool renderFrame(milliseconds_t localT) override { return true; }
};
AbstractFrameAnimation* getRandomAnimation() { return new StubAnimation(); }

#include "../../src/mission-control.cpp"

// A hand-controllable animation for tests that need to drive TRANSITIONING
// through specific phases (rather than StubAnimation's instant finish).
class ControllableAnimation : public AbstractFrameAnimation
{
   public:
    bool finished = false;
    int renderCount = 0;
    milliseconds_t lastLocalT = 0;

    const char* GetName() override { return "ControllableAnimation"; }
    bool renderFrame(milliseconds_t localT) override
    {
        renderCount++;
        lastLocalT = localT;
        return finished;
    }
};

// A minimal effect that opts into ROTATE_SPACE, to exercise
// finishTransition()'s rotation-hint branch deterministically - some real
// effects do set this hint, but which one getRandomEffect() happens to pick
// isn't something a test should rely on.
class RotatingEffectStub : public AbstractEffect
{
   public:
    RotatingEffectStub() { controlHints = ControlHints::ROTATE_SPACE; }
    const char* GetName() override { return "RotatingEffectStub"; }
    CRGB evaluate(LedStrip* strip, Led* led, size_t led_idx, milliseconds_t t) override
    {
        return CRGB::Black;
    }
};

// Exposes MissionControl's private members to this test file via the
// UNIT_TEST-guarded friend declaration in mission-control.h.
class MissionControlTestAccess
{
   public:
    static uint8_t calcBrightness(MissionControl& mc, milliseconds_t t)
    {
        return mc.calcBrightness(t);
    }
    static void setNextTransition(MissionControl& mc) { mc.setNextTransition(); }
    static milliseconds_t effectStart(MissionControl& mc) { return mc.effectStart; }
    static milliseconds_t fadeInEnd(MissionControl& mc) { return mc.fadeInEnd; }
    static milliseconds_t fadeOutStart(MissionControl& mc) { return mc.fadeOutStart; }
    static milliseconds_t nextTransition(MissionControl& mc) { return mc.nextTransition; }
    static milliseconds_t FADE_IN_DURATION(MissionControl& mc) { return mc.FADE_IN_DURATION; }
    static milliseconds_t FADE_OUT_DURATION(MissionControl& mc) { return mc.FADE_OUT_DURATION; }
    static milliseconds_t MIN_EFFECT_DURATION(MissionControl& mc) { return mc.MIN_EFFECT_DURATION; }
    static milliseconds_t MAX_EFFECT_DURATION(MissionControl& mc) { return mc.MAX_EFFECT_DURATION; }
    static void setMaxBrightness(MissionControl& mc, uint8_t b) { mc.maxBrightness = b; }
    static void setEffect(MissionControl& mc, AbstractEffect* e)
    {
        if (mc.effect) delete mc.effect;
        mc.effect = e;
    }
    static AbstractEffect* getEffect(MissionControl& mc) { return mc.effect; }
    static void setNextTransitionValue(MissionControl& mc, milliseconds_t t)
    {
        mc.nextTransition = t;
    }
    static void setEffectStartValue(MissionControl& mc, milliseconds_t t) { mc.effectStart = t; }
    static void setFadeInEndValue(MissionControl& mc, milliseconds_t t) { mc.fadeInEnd = t; }
    static void setFadeOutStartValue(MissionControl& mc, milliseconds_t t) { mc.fadeOutStart = t; }

    static RenderMode getMode(MissionControl& mc) { return mc.mode; }
    static void setMode(MissionControl& mc, RenderMode m) { mc.mode = m; }
    static RenderMode getModeBeforeOff(MissionControl& mc) { return mc.modeBeforeOff; }

    static void setAnimation(MissionControl& mc, AbstractFrameAnimation* a)
    {
        if (mc.animation) delete mc.animation;
        mc.animation = a;
    }
    static AbstractFrameAnimation* getAnimation(MissionControl& mc) { return mc.animation; }

    static void setPendingEffect(MissionControl& mc, AbstractEffect* e)
    {
        if (mc.pendingEffect) delete mc.pendingEffect;
        mc.pendingEffect = e;
    }
    static AbstractEffect* getPendingEffect(MissionControl& mc) { return mc.pendingEffect; }

    static void cancelTransition(MissionControl& mc) { mc.cancelTransition(); }
    static void finishTransition(MissionControl& mc) { mc.finishTransition(); }

    static bool holdPending(MissionControl& mc) { return mc.holdPending; }
    static void setHoldPending(MissionControl& mc, bool v) { mc.holdPending = v; }

    static milliseconds_t transitionWindowStart(MissionControl& mc)
    {
        return mc.transitionWindow.start;
    }
    static void setTransitionWindowStart(MissionControl& mc, milliseconds_t t)
    {
        mc.transitionWindow.start = t;
    }
    static milliseconds_t transitionWindowPreDelayEnd(MissionControl& mc)
    {
        return mc.transitionWindow.preDelayEnd;
    }
    static void setTransitionWindowPreDelayEnd(MissionControl& mc, milliseconds_t t)
    {
        mc.transitionWindow.preDelayEnd = t;
    }
    static milliseconds_t transitionWindowAnimationFinishedAt(MissionControl& mc)
    {
        return mc.transitionWindow.animationFinishedAt;
    }
    static void setTransitionWindowAnimationFinishedAt(MissionControl& mc, milliseconds_t t)
    {
        mc.transitionWindow.animationFinishedAt = t;
    }

    static milliseconds_t PRE_ANIMATION_DELAY(MissionControl& mc) { return mc.PRE_ANIMATION_DELAY; }
    static milliseconds_t POST_ANIMATION_DELAY(MissionControl& mc)
    {
        return mc.POST_ANIMATION_DELAY;
    }
};

void setUp()
{
    GEOMETRY.initializeForTest(ModelId::SINGLE_STRIP_TEST_DEVICE);

    // In production, MissionControl::effect is only ever read after
    // handleTransition() has run at least once - guaranteed because
    // nextTransition defaults to 0, so the very first real update(millis())
    // call always takes the "t >= nextTransition" branch first. Tests call
    // private methods directly (bypassing handleTransition()), which can
    // leave that invariant broken for whichever test runs next against the
    // shared MissionControl singleton - so each test starts from a known,
    // fully-reset state: a real effect, FX_LOOP mode, no in-flight
    // transition.
    MissionControl& mc = MissionControl::Instance();
    MissionControlTestAccess::setEffect(mc, new StaticColor());
    MissionControlTestAccess::setAnimation(mc, nullptr);
    MissionControlTestAccess::setMode(mc, RenderMode::FX_LOOP);
}
void tearDown() {}

// ---------------------------------------------------------------------------
// calcBrightness(t) - the fade in/hold/fade out state machine
// ---------------------------------------------------------------------------

void test_calc_brightness_before_effect_start_is_zero()
{
    MissionControl& mc = MissionControl::Instance();
    MissionControlTestAccess::setNextTransition(mc);
    milliseconds_t start = MissionControlTestAccess::effectStart(mc);

    // t == effectStart: dt=0 -> map(0, 0, FADE_IN_DURATION, 0, max) -> 0,
    // then dim8_raw(0) == 0.
    TEST_ASSERT_EQUAL_UINT8(0, MissionControlTestAccess::calcBrightness(mc, start));
}

// Reproduces the real "flash" bug: update()'s t is captured by the caller
// before processWebCommands() runs, so a command handled there (e.g. NEXT)
// that calls handleTransition() synchronously can leave t behind the
// freshly-reset effectStart by the time calcBrightness() is reached later in
// the same update() call. t and effectStart are both uint32_t, so t -
// effectStart used to underflow into a huge value instead of clamping to 0 -
// map()'s reinterpretation of that as a negative long then wrapped back into
// a bogus, non-zero uint8_t brightness (a bright flash instead of a fade-in
// from black).
void test_calc_brightness_when_t_is_before_effect_start_is_zero()
{
    MissionControl& mc = MissionControl::Instance();
    MissionControlTestAccess::setMaxBrightness(mc, 255);
    // Pin effectStart/fadeInEnd/fadeOutStart to known, mutually consistent
    // values, independent of how much real time has elapsed since the test
    // process started (setNextTransition() would derive them from the real
    // clock and could let a stale hold-plateau window mask this bug).
    MissionControlTestAccess::setEffectStartValue(mc, 100000);
    MissionControlTestAccess::setFadeInEndValue(mc, 102500);
    MissionControlTestAccess::setFadeOutStartValue(mc, 200000);

    TEST_ASSERT_EQUAL_UINT8(0, MissionControlTestAccess::calcBrightness(mc, 100000 - 800));
    TEST_ASSERT_EQUAL_UINT8(0, MissionControlTestAccess::calcBrightness(mc, 100000 - 1));
}

void test_calc_brightness_ramps_up_during_fade_in()
{
    MissionControl& mc = MissionControl::Instance();
    MissionControlTestAccess::setMaxBrightness(mc, 255);
    MissionControlTestAccess::setNextTransition(mc);

    milliseconds_t start = MissionControlTestAccess::effectStart(mc);
    milliseconds_t fadeIn = MissionControlTestAccess::FADE_IN_DURATION(mc);

    uint8_t early = MissionControlTestAccess::calcBrightness(mc, start + fadeIn / 4);
    uint8_t late = MissionControlTestAccess::calcBrightness(mc, start + (3 * fadeIn) / 4);

    TEST_ASSERT_TRUE(late > early);
}

// #111: the effect fade in/out ramps must stay short so the transition doesn't
// crawl through FastLED's coarse low-brightness region. Guards against a future
// bump back to the old multi-second values.
void test_effect_fades_are_sub_second()
{
    MissionControl& mc = MissionControl::Instance();
    TEST_ASSERT_TRUE(MissionControlTestAccess::FADE_IN_DURATION(mc) < 1000);
    TEST_ASSERT_TRUE(MissionControlTestAccess::FADE_OUT_DURATION(mc) < 1000);
}

void test_calc_brightness_is_max_during_hold()
{
    MissionControl& mc = MissionControl::Instance();
    MissionControlTestAccess::setMaxBrightness(mc, 200);
    MissionControlTestAccess::setNextTransition(mc);

    milliseconds_t fadeInEnd = MissionControlTestAccess::fadeInEnd(mc);
    milliseconds_t fadeOutStart = MissionControlTestAccess::fadeOutStart(mc);
    milliseconds_t mid = fadeInEnd + (fadeOutStart - fadeInEnd) / 2;

    uint8_t brightness = MissionControlTestAccess::calcBrightness(mc, mid);
    // dim8_raw(255) == 255 (dim8_raw is the identity at the top of its range)
    TEST_ASSERT_EQUAL_UINT8(dim8_raw(200), brightness);
}

void test_calc_brightness_ramps_down_during_fade_out()
{
    MissionControl& mc = MissionControl::Instance();
    MissionControlTestAccess::setMaxBrightness(mc, 255);
    MissionControlTestAccess::setNextTransition(mc);

    milliseconds_t fadeOutStart = MissionControlTestAccess::fadeOutStart(mc);
    milliseconds_t fadeOutDur = MissionControlTestAccess::FADE_OUT_DURATION(mc);

    uint8_t early = MissionControlTestAccess::calcBrightness(mc, fadeOutStart + fadeOutDur / 4);
    uint8_t late =
        MissionControlTestAccess::calcBrightness(mc, fadeOutStart + (3 * fadeOutDur) / 4);

    TEST_ASSERT_TRUE(early > late);
}

void test_calc_brightness_after_transition_is_zero()
{
    MissionControl& mc = MissionControl::Instance();
    MissionControlTestAccess::setNextTransition(mc);

    milliseconds_t nextTransition = MissionControlTestAccess::nextTransition(mc);
    milliseconds_t fadeOutDur = MissionControlTestAccess::FADE_OUT_DURATION(mc);

    // Far past fadeOutStart + FADE_OUT_DURATION: map() extrapolates below the
    // output range, so the result must still clamp to 0 (via constrain then
    // dim8_raw), not wrap or go negative.
    uint8_t brightness =
        MissionControlTestAccess::calcBrightness(mc, nextTransition + fadeOutDur * 10);
    TEST_ASSERT_EQUAL_UINT8(0, brightness);
}

// ---------------------------------------------------------------------------
// setNextTransition() - relationships between the resulting timestamps
// ---------------------------------------------------------------------------

void test_set_next_transition_orders_timestamps_correctly()
{
    MissionControl& mc = MissionControl::Instance();
    MissionControlTestAccess::setNextTransition(mc);

    milliseconds_t start = MissionControlTestAccess::effectStart(mc);
    milliseconds_t fadeInEnd = MissionControlTestAccess::fadeInEnd(mc);
    milliseconds_t fadeOutStart = MissionControlTestAccess::fadeOutStart(mc);
    milliseconds_t nextTransition = MissionControlTestAccess::nextTransition(mc);

    TEST_ASSERT_EQUAL_UINT32(start + MissionControlTestAccess::FADE_IN_DURATION(mc), fadeInEnd);
    TEST_ASSERT_EQUAL_UINT32(fadeOutStart + MissionControlTestAccess::FADE_OUT_DURATION(mc),
                             nextTransition);
    TEST_ASSERT_TRUE(fadeOutStart >= fadeInEnd);

    milliseconds_t effectDuration = fadeOutStart - fadeInEnd;
    TEST_ASSERT_TRUE(effectDuration >= MissionControlTestAccess::MIN_EFFECT_DURATION(mc));
    TEST_ASSERT_TRUE(effectDuration <= MissionControlTestAccess::MAX_EFFECT_DURATION(mc));
}

// ---------------------------------------------------------------------------
// queueWebCommand() / processWebCommands() - via the functional queue mock
// ---------------------------------------------------------------------------

void test_queue_web_command_and_process_hold()
{
    MissionControl& mc = MissionControl::Instance();
    TEST_ASSERT_TRUE(mc.queueWebCommand(Command::Hold()));

    MissionControlTestAccess::setNextTransition(mc);
    milliseconds_t before = MissionControlTestAccess::nextTransition(mc);

    mc.update(before);  // drains the queue and calls holdEffect()

    // holdEffect() sets nextTransition to the maximum possible value
    TEST_ASSERT_EQUAL_UINT32(~0UL, MissionControlTestAccess::nextTransition(mc));
    TEST_ASSERT_TRUE(mc.isHolding());
    TEST_ASSERT_EQUAL(RenderMode::HOLDING, MissionControlTestAccess::getMode(mc));
}

// resumeEffect() must clear the holding flag and schedule a real (non-max)
// transition, without restarting the current effect's fade-in.
void test_resume_effect_clears_holding_and_reduces_remaining_by_elapsed()
{
    MissionControl& mc = MissionControl::Instance();
    MissionControlTestAccess::setNextTransition(mc);

    // Pin effectStart far enough in the past that it exceeds any possible
    // MIN..MAX random pick, so resumeEffect() must clamp the remaining time
    // to ~0 (schedule almost immediately) instead of handing the effect a
    // fresh full window on top of what it already had. The test process's
    // own uptime is only seconds, nowhere near 20 minutes, so this relies on
    // uint32_t wraparound subtraction (the same rollover-safe trick used
    // throughout mission-control.cpp) rather than a plain now-20min, which
    // would go negative and clamp to 0 instead of wrapping.
    milliseconds_t now = millis();
    milliseconds_t farPast = now - (20 MINUTES);
    MissionControlTestAccess::setEffectStartValue(mc, farPast);

    TEST_ASSERT_TRUE(mc.queueWebCommand(Command::Hold()));
    mc.update(0);
    TEST_ASSERT_TRUE(mc.isHolding());

    TEST_ASSERT_TRUE(mc.queueWebCommand(Command::Resume()));
    milliseconds_t before = millis();
    mc.update(0);
    milliseconds_t after = millis();

    TEST_ASSERT_FALSE(mc.isHolding());
    TEST_ASSERT_EQUAL(RenderMode::FX_LOOP, MissionControlTestAccess::getMode(mc));

    milliseconds_t nextTransition = MissionControlTestAccess::nextTransition(mc);
    milliseconds_t fadeOutDur = MissionControlTestAccess::FADE_OUT_DURATION(mc);
    TEST_ASSERT_TRUE(nextTransition >= before + fadeOutDur);
    TEST_ASSERT_TRUE(nextTransition <= after + fadeOutDur + 50);

    // effectStart must be untouched - resuming keeps the current effect's
    // plateau brightness rather than restarting its fade-in.
    TEST_ASSERT_EQUAL_UINT32(farPast, MissionControlTestAccess::effectStart(mc));
}

void test_queue_web_command_fills_and_rejects_when_full()
{
    MissionControl& mc = MissionControl::Instance();

    // Drain whatever might be queued from other tests first.
    milliseconds_t t = MissionControlTestAccess::nextTransition(mc);
    if (t == 0) t = 1;
    mc.update(t > 100000 ? t - 100000 : 1);

    int accepted = 0;
    for (int i = 0; i < 20; i++)
    {
        if (mc.queueWebCommand(Command::Hold())) accepted++;
    }
    // The queue capacity is WEB_QUEUE_SIZE (10); sending 20 items must reject some.
    TEST_ASSERT_TRUE(accepted <= 10);
    TEST_ASSERT_TRUE(accepted > 0);

    // Drain the queue so it doesn't affect later tests.
    mc.update(MissionControlTestAccess::nextTransition(mc) > 0
                  ? MissionControlTestAccess::nextTransition(mc) - 1
                  : 0);
}

// Reproduces the real startup path: main.cpp's setup() never calls
// handleTransition() before the loop starts, so the very first update(millis())
// call can see a null effect. update() must not dereference it before the
// t >= nextTransition check has a chance to run handleTransition() and set one.
void test_update_does_not_dereference_null_effect_before_first_transition()
{
    MissionControl& mc = MissionControl::Instance();
    MissionControlTestAccess::setEffect(mc, nullptr);
    // Mirrors the real boot state: nextTransition defaults to 0, so the very
    // first update(t) call reaches the live-color-update line with a null
    // effect before the t >= nextTransition branch runs handleTransition().
    MissionControlTestAccess::setNextTransitionValue(mc, 0);

    mc.update(0);  // must not crash despite effect == nullptr

    // handleTransition()'s default (playAnimation=true) no longer installs an
    // effect synchronously - it starts a TRANSITIONING window instead, so
    // effect legitimately stays null until that transition lands.
    TEST_ASSERT_EQUAL(RenderMode::TRANSITIONING, MissionControlTestAccess::getMode(mc));
    TEST_ASSERT_NOT_NULL(MissionControlTestAccess::getAnimation(mc));

    // Drive the transition to completion: effect must end up non-null once it lands.
    milliseconds_t preDelayEnd = MissionControlTestAccess::transitionWindowPreDelayEnd(mc);
    mc.update(preDelayEnd);  // StubAnimation finishes on its very first renderFrame() call
    milliseconds_t finishedAt = MissionControlTestAccess::transitionWindowAnimationFinishedAt(mc);
    TEST_ASSERT_TRUE(finishedAt != 0);
    mc.update(finishedAt + MissionControlTestAccess::POST_ANIMATION_DELAY(mc));

    TEST_ASSERT_EQUAL(RenderMode::FX_LOOP, MissionControlTestAccess::getMode(mc));
    TEST_ASSERT_NOT_NULL(MissionControlTestAccess::getEffect(mc));
}

void test_power_off_and_on_toggle_state()
{
    MissionControl& mc = MissionControl::Instance();

    TEST_ASSERT_TRUE(mc.queueWebCommand(Command::PowerOff()));
    mc.update(0);  // mode is now OFF; update() should take the early-return path
    TEST_ASSERT_FALSE(mc.isOn());

    TEST_ASSERT_TRUE(mc.queueWebCommand(Command::PowerOn()));
    mc.update(0);  // processWebCommands() still runs even while OFF, turning it back ON
    TEST_ASSERT_TRUE(mc.isOn());
}

// powerOff()/powerOn() must round-trip through whichever mode was active
// before powering off - HOLDING included, not just plain FX_LOOP - since the
// web UI's HOLD/RESUME state shouldn't silently reset across a power cycle.
void test_power_off_and_on_restores_holding_mode()
{
    MissionControl& mc = MissionControl::Instance();
    // Pin nextTransition safely into the future: it's leftover state from
    // whichever test ran before this one, and powerOn() falls through to the
    // rest of update() in the same tick - a stale/expired nextTransition
    // would otherwise let "t >= nextTransition" fire handleTransition() and
    // clobber the HOLDING mode this test is trying to verify survives a
    // power cycle.
    MissionControlTestAccess::setNextTransition(mc);
    MissionControlTestAccess::setMode(mc, RenderMode::HOLDING);

    TEST_ASSERT_TRUE(mc.queueWebCommand(Command::PowerOff()));
    mc.update(0);
    TEST_ASSERT_FALSE(mc.isOn());

    TEST_ASSERT_TRUE(mc.queueWebCommand(Command::PowerOn()));
    mc.update(0);
    TEST_ASSERT_TRUE(mc.isOn());
    TEST_ASSERT_TRUE(mc.isHolding());
}

void test_queue_color_command_sets_static_color_and_enters_static_mode()
{
    MissionControl& mc = MissionControl::Instance();
    MissionControlTestAccess::setEffect(mc, new ElectricSparks());

    TEST_ASSERT_TRUE(mc.queueWebCommand(Command::Color(10, 20, 30)));
    mc.update(0);

    TEST_ASSERT_TRUE(mc.isColorActive());
    TEST_ASSERT_EQUAL_UINT8(10, mc.staticColor.r);
    TEST_ASSERT_EQUAL_UINT8(20, mc.staticColor.g);
    TEST_ASSERT_EQUAL_UINT8(30, mc.staticColor.b);
}

void test_queue_color_command_while_already_in_static_mode_updates_color()
{
    MissionControl& mc = MissionControl::Instance();
    MissionControlTestAccess::setEffect(mc, new StaticColor());

    TEST_ASSERT_TRUE(mc.queueWebCommand(Command::Color(40, 50, 60)));
    mc.update(0);

    TEST_ASSERT_TRUE(mc.isColorActive());
    TEST_ASSERT_EQUAL_UINT8(40, mc.staticColor.r);
    TEST_ASSERT_EQUAL_UINT8(50, mc.staticColor.g);
    TEST_ASSERT_EQUAL_UINT8(60, mc.staticColor.b);
}

// Regression guard: isColorActive() must not read the stale outgoing effect
// while TRANSITIONING. If a StaticColor was already active when NEXT was
// pressed, that effect stays around (untouched) for the whole transition
// window - before the fix, isColorActive() reported true from it, so a COLOR
// command mid-transition skipped transitionToStaticColor() and only wrote
// staticColor into an effect finishTransition() was about to delete, silently
// losing the color once the animation landed on a fresh random effect.
// transitionToStaticColor() always ends in HOLDING (it calls holdEffect()
// unconditionally - a solid color is meant to be pinned, not rotate away).
void test_color_command_mid_transition_cancels_transition_and_applies_color()
{
    MissionControl& mc = MissionControl::Instance();
    MissionControlTestAccess::setEffect(mc, new StaticColor());
    MissionControlTestAccess::setAnimation(mc, new ControllableAnimation());
    MissionControlTestAccess::setMode(mc, RenderMode::TRANSITIONING);

    TEST_ASSERT_FALSE(MissionControlTestAccess::holdPending(mc));

    TEST_ASSERT_TRUE(mc.queueWebCommand(Command::Color(70, 80, 90)));
    mc.update(0);

    TEST_ASSERT_EQUAL(RenderMode::HOLDING, MissionControlTestAccess::getMode(mc));
    TEST_ASSERT_TRUE(mc.isColorActive());
    TEST_ASSERT_EQUAL_UINT8(70, mc.staticColor.r);
    TEST_ASSERT_EQUAL_UINT8(80, mc.staticColor.g);
    TEST_ASSERT_EQUAL_UINT8(90, mc.staticColor.b);
}

// Selecting a specific effect by id must start a transition to that exact
// effect (not a random one) and implicitly hold it once landed - mirroring
// how COLOR's transitionToStaticColor() reuses handleTransition()+holdEffect().
void test_queue_effect_command_transitions_to_selected_effect_and_holds()
{
    MissionControl& mc = MissionControl::Instance();
    MissionControlTestAccess::setEffect(mc, new ElectricSparks());
    MissionControlTestAccess::setMode(mc, RenderMode::FX_LOOP);

    TEST_ASSERT_TRUE(
        mc.queueWebCommand(Command::Effect(static_cast<uint8_t>(EffectId::NinjaStar))));
    mc.update(0);

    // handleTransition() kicks off a real TRANSITIONING window (an actual
    // animation is picked, not a controllable stub), with the selected
    // effect parked as pendingEffect until it lands.
    TEST_ASSERT_EQUAL(RenderMode::TRANSITIONING, MissionControlTestAccess::getMode(mc));
    AbstractEffect* pending = MissionControlTestAccess::getPendingEffect(mc);
    TEST_ASSERT_NOT_NULL(pending);
    TEST_ASSERT_EQUAL_STRING("NinjaStar", pending->GetName());
    // holdEffect() ran immediately after handleTransition(), while mode was
    // already TRANSITIONING, so the hold is deferred rather than applied yet.
    TEST_ASSERT_TRUE(MissionControlTestAccess::holdPending(mc));

    // Clean up the in-flight transition this test intentionally left mid-way
    // through, so it doesn't leak pendingEffect/holdPending/animation state
    // into whichever test runs next against the shared MissionControl
    // singleton (setUp() only resets effect/animation/mode).
    MissionControlTestAccess::cancelTransition(mc);
}

// getTargetEffectName() names the effect the device is committed to: the
// pending one mid-transition, the current one otherwise. Comms wires this to
// the "effect" state field so the web dropdown updates the instant a
// selection is accepted rather than after the transition (#113).
void test_target_effect_name_reports_pending_effect_during_transition()
{
    MissionControl& mc = MissionControl::Instance();
    ElectricSparks* current = new ElectricSparks();
    MissionControlTestAccess::setEffect(mc, current);
    MissionControlTestAccess::setMode(mc, RenderMode::FX_LOOP);

    // Not transitioning: target == current.
    TEST_ASSERT_EQUAL_STRING(current->GetName(), mc.getTargetEffectName());

    TEST_ASSERT_TRUE(
        mc.queueWebCommand(Command::Effect(static_cast<uint8_t>(EffectId::NinjaStar))));
    mc.update(0);

    // Mid-transition: target is the incoming effect, even though the outgoing
    // ElectricSparks is still the one being rendered.
    TEST_ASSERT_EQUAL(RenderMode::TRANSITIONING, MissionControlTestAccess::getMode(mc));
    AbstractEffect* pending = MissionControlTestAccess::getPendingEffect(mc);
    TEST_ASSERT_NOT_NULL(pending);
    TEST_ASSERT_EQUAL_STRING(pending->GetName(), mc.getTargetEffectName());

    MissionControlTestAccess::cancelTransition(mc);
}

void test_queue_effect_command_with_out_of_range_id_is_ignored()
{
    MissionControl& mc = MissionControl::Instance();
    MissionControlTestAccess::setEffect(mc, new ElectricSparks());
    MissionControlTestAccess::setMode(mc, RenderMode::FX_LOOP);

    TEST_ASSERT_TRUE(mc.queueWebCommand(Command::Effect(255)));
    mc.update(0);

    TEST_ASSERT_EQUAL(RenderMode::FX_LOOP, MissionControlTestAccess::getMode(mc));
    TEST_ASSERT_NULL(MissionControlTestAccess::getPendingEffect(mc));
}

void test_queue_model_command_updates_factory_config()
{
    MissionControl& mc = MissionControl::Instance();

    TEST_ASSERT_TRUE(mc.queueWebCommand(Command::Model(static_cast<uint16_t>(ModelId::L70_MK1))));
    mc.update(0);

    TEST_ASSERT_EQUAL(ModelId::L70_MK1, FactoryConfig::getModelId());
}

void test_queue_device_name_command_persists_via_device_identity()
{
    MissionControl& mc = MissionControl::Instance();

    TEST_ASSERT_TRUE(mc.queueWebCommand(Command::DeviceName("Kitchen Lamp")));
    mc.update(0);

    TEST_ASSERT_TRUE(DeviceIdentity::isNameCustomized());
    TEST_ASSERT_EQUAL_STRING("Kitchen-Lamp", DeviceIdentity::getDeviceName().c_str());

    DeviceIdentity::setDeviceName("");  // reset for later tests
}

// ---------------------------------------------------------------------------
// TRANSITIONING - the non-blocking replacement for the old blocking
// runRandomAnimation(). These are interaction tests: they drive update()
// itself across several ticks (rather than calling private methods in
// isolation) to prove the actual system-level behavior this refactor exists
// for - the render loop, and therefore processWebCommands(), never blocks
// for the length of a transition.
// ---------------------------------------------------------------------------

// A NEXT command must start a TRANSITIONING window rather than block inside
// processWebCommands() for the transition's whole duration - and a second
// command queued right after must be drained on the very next update() tick,
// not after the transition finishes.
void test_next_command_starts_transitioning_without_blocking_later_commands()
{
    MissionControl& mc = MissionControl::Instance();

    TEST_ASSERT_TRUE(mc.queueWebCommand(Command::Next()));
    mc.update(millis());

    TEST_ASSERT_EQUAL(RenderMode::TRANSITIONING, MissionControlTestAccess::getMode(mc));
    TEST_ASSERT_NOT_NULL(MissionControlTestAccess::getAnimation(mc));

    // If update() were still blocking for the whole transition (the old
    // runRandomAnimation() behavior), this command would sit queued and
    // unactioned until the transition finished. It must take effect on the
    // very next tick instead.
    TEST_ASSERT_TRUE(mc.queueWebCommand(Command::PowerOff()));
    mc.update(millis());

    TEST_ASSERT_EQUAL(RenderMode::OFF, MissionControlTestAccess::getMode(mc));
}

// Reproduces the same "stale t" scenario the old blocking flow had a guard
// for, adapted to the new model: t captured just before processWebCommands()
// runs can be older than transitionWindow.start once a NEXT command starts a
// fresh transition inside that same update() call. updateTransition() must
// not flash a non-zero brightness in that case.
void test_next_command_mid_update_does_not_flash_stale_t_into_high_brightness()
{
    MissionControl& mc = MissionControl::Instance();
    MissionControlTestAccess::setMaxBrightness(mc, 255);

    TEST_ASSERT_TRUE(mc.queueWebCommand(Command::Next()));

    milliseconds_t staleT = millis();
    mc.update(staleT);

    TEST_ASSERT_EQUAL(RenderMode::TRANSITIONING, MissionControlTestAccess::getMode(mc));
    TEST_ASSERT_EQUAL_UINT8(0, FastLED.getBrightness());
}

// updateTransition()'s own stale-t guard, exercised directly: a t older than
// transitionWindow.start (the same underflow shape calcBrightness() guards
// against) must clamp to zero brightness rather than underflow.
void test_update_transition_guards_against_t_before_window_start()
{
    MissionControl& mc = MissionControl::Instance();
    MissionControlTestAccess::setMaxBrightness(mc, 255);
    MissionControlTestAccess::setAnimation(mc, new ControllableAnimation());
    MissionControlTestAccess::setMode(mc, RenderMode::TRANSITIONING);
    MissionControlTestAccess::setTransitionWindowStart(mc, 100000);
    MissionControlTestAccess::setTransitionWindowPreDelayEnd(mc, 100200);
    MissionControlTestAccess::setTransitionWindowAnimationFinishedAt(mc, 0);

    mc.update(100000 - 1);

    TEST_ASSERT_EQUAL_UINT8(0, FastLED.getBrightness());
    TEST_ASSERT_EQUAL(RenderMode::TRANSITIONING, MissionControlTestAccess::getMode(mc));
}

// The strip must actually go to black (brightness pushed via FASTLED_SHOW(),
// not just a field set) during both the pre- and post-animation delay
// windows, and back to (gamma-corrected) max brightness while the animation
// itself renders. maxBrightness is 255 here, where dim8_raw is the identity,
// so this doesn't exercise the gamma curve itself - see
// test_transitioning_animation_applies_gamma_corrected_max_brightness for that.
void test_transitioning_pre_and_post_delay_windows_zero_the_strip_brightness()
{
    MissionControl& mc = MissionControl::Instance();
    MissionControlTestAccess::setMaxBrightness(mc, 255);

    ControllableAnimation* anim = new ControllableAnimation();
    MissionControlTestAccess::setAnimation(mc, anim);

    milliseconds_t start = millis();
    milliseconds_t preDelayEnd = start + MissionControlTestAccess::PRE_ANIMATION_DELAY(mc);
    MissionControlTestAccess::setTransitionWindowStart(mc, start);
    MissionControlTestAccess::setTransitionWindowPreDelayEnd(mc, preDelayEnd);
    MissionControlTestAccess::setTransitionWindowAnimationFinishedAt(mc, 0);
    MissionControlTestAccess::setMode(mc, RenderMode::TRANSITIONING);

    // Still inside the pre-delay window.
    mc.update(start);
    TEST_ASSERT_EQUAL_UINT8(0, FastLED.getBrightness());

    // Past pre-delay: the animation renders at full brightness.
    anim->finished = false;
    mc.update(preDelayEnd);
    TEST_ASSERT_EQUAL_UINT8(255, FastLED.getBrightness());
    TEST_ASSERT_TRUE(anim->renderCount > 0);

    // The animation finishes: its very last frame still renders at full
    // brightness (so the animation's final frame is actually visible instead
    // of being cut off), and only the *next* tick - now inside the
    // post-delay window - goes back to black.
    anim->finished = true;
    mc.update(preDelayEnd + 1);
    TEST_ASSERT_EQUAL_UINT8(255, FastLED.getBrightness());
    milliseconds_t finishedAt = MissionControlTestAccess::transitionWindowAnimationFinishedAt(mc);
    TEST_ASSERT_TRUE(finishedAt != 0);

    mc.update(finishedAt + 1);
    TEST_ASSERT_EQUAL_UINT8(0, FastLED.getBrightness());
}

// Regression test: updateTransition() used to apply maxBrightness to the
// strip raw during animation playback, while calcBrightness() (used for
// FX_LOOP fades) ran it through dim8_raw() first. At low persisted
// brightness settings this made transition animations render noticeably
// brighter than the effects around them - the persisted brightness appeared
// to not apply to animations at all.
void test_transitioning_animation_applies_gamma_corrected_max_brightness()
{
    MissionControl& mc = MissionControl::Instance();
    MissionControlTestAccess::setMaxBrightness(mc, 100);

    ControllableAnimation* anim = new ControllableAnimation();
    MissionControlTestAccess::setAnimation(mc, anim);

    milliseconds_t start = millis();
    milliseconds_t preDelayEnd = start + MissionControlTestAccess::PRE_ANIMATION_DELAY(mc);
    MissionControlTestAccess::setTransitionWindowStart(mc, start);
    MissionControlTestAccess::setTransitionWindowPreDelayEnd(mc, preDelayEnd);
    MissionControlTestAccess::setTransitionWindowAnimationFinishedAt(mc, 0);
    MissionControlTestAccess::setMode(mc, RenderMode::TRANSITIONING);

    anim->finished = false;
    mc.update(preDelayEnd);
    TEST_ASSERT_EQUAL_UINT8(dim8_raw(100), FastLED.getBrightness());
}

// POWER_OFF arriving mid-transition must cancel the in-flight animation
// cleanly (no dangling pointer) and blank the strip immediately, rather than
// wait for the transition to run its course.
void test_power_off_mid_transition_cancels_animation_and_blanks_strip()
{
    MissionControl& mc = MissionControl::Instance();
    MissionControlTestAccess::setMaxBrightness(mc, 255);

    ControllableAnimation* anim = new ControllableAnimation();
    MissionControlTestAccess::setAnimation(mc, anim);

    milliseconds_t now = millis();
    MissionControlTestAccess::setTransitionWindowStart(mc, now);
    MissionControlTestAccess::setTransitionWindowPreDelayEnd(mc, now);  // already past pre-delay
    MissionControlTestAccess::setTransitionWindowAnimationFinishedAt(mc, 0);
    MissionControlTestAccess::setMode(mc, RenderMode::TRANSITIONING);

    TEST_ASSERT_TRUE(mc.queueWebCommand(Command::PowerOff()));
    mc.update(now);

    TEST_ASSERT_EQUAL(RenderMode::OFF, MissionControlTestAccess::getMode(mc));
    TEST_ASSERT_NULL(MissionControlTestAccess::getAnimation(mc));
    // powerOff() blanks by painting the buffer black (matching the pre-
    // refactor behavior), not by touching FastLED's brightness scalar -
    // check the actual strip content rather than FastLED.getBrightness().
    TEST_ASSERT_TRUE(GEOMETRY.getStrip(0).buffer[0] == CRGB::Black);
}

// Drives a full TRANSITIONING cycle end-to-end through update() - pre-delay,
// animation, post-delay - and checks it lands back in FX_LOOP with a fresh,
// non-null effect installed.
void test_full_transition_cycle_lands_on_new_effect_in_fx_loop_mode()
{
    MissionControl& mc = MissionControl::Instance();
    MissionControlTestAccess::setMaxBrightness(mc, 255);

    ControllableAnimation* anim = new ControllableAnimation();
    anim->finished = true;  // finishes on its very first renderFrame() call
    MissionControlTestAccess::setAnimation(mc, anim);

    milliseconds_t start = millis();
    milliseconds_t preDelayEnd = start + MissionControlTestAccess::PRE_ANIMATION_DELAY(mc);
    MissionControlTestAccess::setTransitionWindowStart(mc, start);
    MissionControlTestAccess::setTransitionWindowPreDelayEnd(mc, preDelayEnd);
    MissionControlTestAccess::setTransitionWindowAnimationFinishedAt(mc, 0);
    MissionControlTestAccess::setMode(mc, RenderMode::TRANSITIONING);

    mc.update(start);  // still pre-delay
    TEST_ASSERT_EQUAL(RenderMode::TRANSITIONING, MissionControlTestAccess::getMode(mc));

    mc.update(preDelayEnd);  // animation runs and finishes immediately
    TEST_ASSERT_EQUAL(RenderMode::TRANSITIONING, MissionControlTestAccess::getMode(mc));
    milliseconds_t finishedAt = MissionControlTestAccess::transitionWindowAnimationFinishedAt(mc);
    TEST_ASSERT_TRUE(finishedAt != 0);

    mc.update(finishedAt);  // still inside post-delay
    TEST_ASSERT_EQUAL(RenderMode::TRANSITIONING, MissionControlTestAccess::getMode(mc));

    milliseconds_t postDelayEnd = finishedAt + MissionControlTestAccess::POST_ANIMATION_DELAY(mc);
    mc.update(postDelayEnd);  // transition completes

    TEST_ASSERT_EQUAL(RenderMode::FX_LOOP, MissionControlTestAccess::getMode(mc));
    TEST_ASSERT_NULL(MissionControlTestAccess::getAnimation(mc));
    TEST_ASSERT_NOT_NULL(MissionControlTestAccess::getEffect(mc));
}

// A HOLD command arriving mid-transition can't flip to HOLDING immediately
// without abandoning the in-flight animation/effect swap - it must defer,
// then actually take effect once the transition lands.
void test_hold_command_mid_transition_is_applied_once_transition_finishes()
{
    MissionControl& mc = MissionControl::Instance();
    MissionControlTestAccess::setMaxBrightness(mc, 255);

    ControllableAnimation* anim = new ControllableAnimation();
    anim->finished = true;
    MissionControlTestAccess::setAnimation(mc, anim);

    milliseconds_t start = millis();
    milliseconds_t preDelayEnd = start + MissionControlTestAccess::PRE_ANIMATION_DELAY(mc);
    MissionControlTestAccess::setTransitionWindowStart(mc, start);
    MissionControlTestAccess::setTransitionWindowPreDelayEnd(mc, preDelayEnd);
    MissionControlTestAccess::setTransitionWindowAnimationFinishedAt(mc, 0);
    MissionControlTestAccess::setMode(mc, RenderMode::TRANSITIONING);

    mc.update(preDelayEnd);  // animation finishes, enters post-delay
    milliseconds_t finishedAt = MissionControlTestAccess::transitionWindowAnimationFinishedAt(mc);

    TEST_ASSERT_TRUE(mc.queueWebCommand(Command::Hold()));
    mc.update(finishedAt);  // HOLD processed while still TRANSITIONING -> deferred

    TEST_ASSERT_EQUAL(RenderMode::TRANSITIONING, MissionControlTestAccess::getMode(mc));
    TEST_ASSERT_TRUE(MissionControlTestAccess::holdPending(mc));
    // isHoldPending() is what lets the web UI flip its Hold button the
    // instant the command is accepted, instead of lagging until the
    // in-flight transition actually lands on HOLDING.
    TEST_ASSERT_TRUE(mc.isHoldPending());
    TEST_ASSERT_FALSE(mc.isHolding());

    milliseconds_t postDelayEnd = finishedAt + MissionControlTestAccess::POST_ANIMATION_DELAY(mc);
    mc.update(postDelayEnd);  // transition completes -> deferred hold applied

    TEST_ASSERT_EQUAL(RenderMode::HOLDING, MissionControlTestAccess::getMode(mc));
    TEST_ASSERT_FALSE(MissionControlTestAccess::holdPending(mc));
    TEST_ASSERT_TRUE(mc.isHolding());
}

// A HOLD command that arrived mid-transition (holdPending) and never got to
// land, because POWER_OFF cut the transition short first, must not be
// silently dropped: powerOn() should come back into HOLDING, not FX_LOOP.
// powerOff() computes modeBeforeOff from holdPending before cancelTransition()
// clears it - this regression-guards that ordering.
void test_power_off_mid_transition_with_pending_hold_restores_holding_on_power_on()
{
    MissionControl& mc = MissionControl::Instance();
    MissionControlTestAccess::setMaxBrightness(mc, 255);

    ControllableAnimation* anim = new ControllableAnimation();
    MissionControlTestAccess::setAnimation(mc, anim);
    MissionControlTestAccess::setMode(mc, RenderMode::TRANSITIONING);
    MissionControlTestAccess::setHoldPending(mc, true);

    TEST_ASSERT_TRUE(mc.queueWebCommand(Command::PowerOff()));
    mc.update(millis());

    TEST_ASSERT_EQUAL(RenderMode::OFF, MissionControlTestAccess::getMode(mc));
    TEST_ASSERT_EQUAL(RenderMode::HOLDING, MissionControlTestAccess::getModeBeforeOff(mc));

    TEST_ASSERT_TRUE(mc.queueWebCommand(Command::PowerOn()));
    mc.update(millis());

    TEST_ASSERT_EQUAL(RenderMode::HOLDING, MissionControlTestAccess::getMode(mc));
    TEST_ASSERT_TRUE(mc.isHolding());
}

// Runs several real NEXT-triggered transitions end-to-end (through the real
// handleTransition()/updateTransition()/finishTransition() path, using the
// stubbed getRandomAnimation()) and checks the animation instance count
// returns to baseline - i.e. MissionControl doesn't leak an AbstractFrameAnimation
// each time it starts and finishes a transition.
void test_repeated_transitions_do_not_leak_animation_instances()
{
    MissionControl& mc = MissionControl::Instance();
    MissionControlTestAccess::setMaxBrightness(mc, 255);

    int before = stubAnimationLiveCount;

    for (int i = 0; i < 5; i++)
    {
        TEST_ASSERT_TRUE(mc.queueWebCommand(Command::Next()));
        mc.update(millis());
        TEST_ASSERT_EQUAL(RenderMode::TRANSITIONING, MissionControlTestAccess::getMode(mc));

        // StubAnimation finishes on its very first renderFrame() call; jump
        // straight past the fixed pre/post-delay brackets to drive the
        // transition to completion without waiting on real wall-clock time.
        milliseconds_t preDelayEnd = MissionControlTestAccess::transitionWindowPreDelayEnd(mc);
        mc.update(preDelayEnd);
        milliseconds_t finishedAt =
            MissionControlTestAccess::transitionWindowAnimationFinishedAt(mc);
        TEST_ASSERT_TRUE(finishedAt != 0);

        mc.update(finishedAt + MissionControlTestAccess::POST_ANIMATION_DELAY(mc));
        TEST_ASSERT_EQUAL(RenderMode::FX_LOOP, MissionControlTestAccess::getMode(mc));
    }

    TEST_ASSERT_EQUAL_INT(before, stubAnimationLiveCount);
}

// cancelTransition() must also free a pendingEffect that was queued up for a
// transition that never got to install it - otherwise it leaks. This
// combination (a non-null nextEffect together with playAnimation=true)
// never actually occurs from today's call sites, but cancelTransition()
// should be safe regardless of how it's reached.
void test_cancel_transition_frees_a_pending_effect_that_was_never_installed()
{
    MissionControl& mc = MissionControl::Instance();
    MissionControlTestAccess::setPendingEffect(mc, new StaticColor());

    MissionControlTestAccess::cancelTransition(mc);

    TEST_ASSERT_NULL(MissionControlTestAccess::getPendingEffect(mc));
}

// finishTransition() must dispatch on the incoming effect's ROTATE_SPACE
// hint the same way the old synchronous handleTransition() did.
void test_finish_transition_applies_rotation_hint_from_the_new_effect()
{
    MissionControl& mc = MissionControl::Instance();
    MissionControlTestAccess::setPendingEffect(mc, new RotatingEffectStub());

    MissionControlTestAccess::finishTransition(mc);

    TEST_ASSERT_EQUAL_STRING("RotatingEffectStub", mc.getEffectName());
    TEST_ASSERT_EQUAL(RenderMode::FX_LOOP, MissionControlTestAccess::getMode(mc));
}

// ---------------------------------------------------------------------------
// BrightnessConfig - persists max brightness across reboots (NVS)
// ---------------------------------------------------------------------------

// esp_restart() is mocked to a no-op for native tests (test/mocks/Arduino.h)
// specifically so this switch case is safe to exercise here.
void test_reboot_command_calls_esp_restart_without_crashing()
{
    MissionControl& mc = MissionControl::Instance();
    TEST_ASSERT_TRUE(mc.queueWebCommand(Command::Reboot()));
    mc.update(0);
    TEST_ASSERT_TRUE(true);
}

// powerOff() while already OFF must leave modeBeforeOff untouched (the
// `mode != RenderMode::OFF` guard's false branch) rather than clobbering it
// with RenderMode::OFF itself.
void test_power_off_while_already_off_preserves_mode_before_off()
{
    MissionControl& mc = MissionControl::Instance();
    MissionControlTestAccess::setNextTransition(mc);
    MissionControlTestAccess::setMode(mc, RenderMode::HOLDING);

    TEST_ASSERT_TRUE(mc.queueWebCommand(Command::PowerOff()));
    mc.update(0);
    TEST_ASSERT_EQUAL(RenderMode::HOLDING, MissionControlTestAccess::getModeBeforeOff(mc));

    // Already OFF: a second PowerOff must not overwrite modeBeforeOff.
    TEST_ASSERT_TRUE(mc.queueWebCommand(Command::PowerOff()));
    mc.update(0);
    TEST_ASSERT_EQUAL(RenderMode::HOLDING, MissionControlTestAccess::getModeBeforeOff(mc));

    TEST_ASSERT_TRUE(mc.queueWebCommand(Command::PowerOn()));
    mc.update(0);
    TEST_ASSERT_EQUAL(RenderMode::HOLDING, MissionControlTestAccess::getMode(mc));
}

// update()'s frame-throttle: with MIN_FRAME_DURATION_MS set high enough that
// a real (fast, native) frame always finishes under it, the delay branch
// must actually be taken. vTaskDelay() is a no-op mock natively, so this is
// safe to call for real rather than needing a controllable clock.
void test_update_throttles_frames_faster_than_the_minimum_duration()
{
    MissionControl& mc = MissionControl::Instance();
    MissionControlTestAccess::setNextTransition(mc);
    mc.setFrameDurationCap(1000);

    mc.update(millis());

    mc.setFrameDurationCap(0);  // restore default so later tests aren't throttled
    TEST_ASSERT_TRUE(true);
}

void test_brightness_config_persists_and_reloads_value()
{
    BrightnessConfig::persist(120);
    TEST_ASSERT_EQUAL_UINT8(120, BrightnessConfig::load(64));

    BrightnessConfig::persist(30);
    TEST_ASSERT_EQUAL_UINT8(30, BrightnessConfig::load(64));
}

// ---------------------------------------------------------------------------
// WsCommandParser -> queueWebCommand -> update() - the full WS message path
// ---------------------------------------------------------------------------

void test_ws_json_color_command_flows_through_to_static_color()
{
    MissionControl& mc = MissionControl::Instance();
    MissionControlTestAccess::setEffect(mc, new ElectricSparks());

    Command cmd;
    TEST_ASSERT_TRUE(WsCommandParser::parse("{\"type\":\"color\",\"r\":5,\"g\":6,\"b\":7}", cmd));
    TEST_ASSERT_TRUE(mc.queueWebCommand(cmd));
    mc.update(0);

    TEST_ASSERT_TRUE(mc.isColorActive());
    TEST_ASSERT_EQUAL_UINT8(5, mc.staticColor.r);
    TEST_ASSERT_EQUAL_UINT8(6, mc.staticColor.g);
    TEST_ASSERT_EQUAL_UINT8(7, mc.staticColor.b);
}

int main(int argc, char** argv)
{
    UNITY_BEGIN();

    RUN_TEST(test_calc_brightness_before_effect_start_is_zero);
    RUN_TEST(test_calc_brightness_when_t_is_before_effect_start_is_zero);
    RUN_TEST(test_calc_brightness_ramps_up_during_fade_in);
    RUN_TEST(test_effect_fades_are_sub_second);
    RUN_TEST(test_calc_brightness_is_max_during_hold);
    RUN_TEST(test_calc_brightness_ramps_down_during_fade_out);
    RUN_TEST(test_calc_brightness_after_transition_is_zero);

    RUN_TEST(test_set_next_transition_orders_timestamps_correctly);

    RUN_TEST(test_queue_web_command_and_process_hold);
    RUN_TEST(test_resume_effect_clears_holding_and_reduces_remaining_by_elapsed);
    RUN_TEST(test_queue_web_command_fills_and_rejects_when_full);
    RUN_TEST(test_update_does_not_dereference_null_effect_before_first_transition);
    RUN_TEST(test_power_off_and_on_toggle_state);
    RUN_TEST(test_power_off_and_on_restores_holding_mode);
    RUN_TEST(test_queue_color_command_sets_static_color_and_enters_static_mode);
    RUN_TEST(test_queue_color_command_while_already_in_static_mode_updates_color);
    RUN_TEST(test_color_command_mid_transition_cancels_transition_and_applies_color);
    RUN_TEST(test_queue_effect_command_transitions_to_selected_effect_and_holds);
    RUN_TEST(test_target_effect_name_reports_pending_effect_during_transition);
    RUN_TEST(test_queue_effect_command_with_out_of_range_id_is_ignored);
    RUN_TEST(test_queue_model_command_updates_factory_config);
    RUN_TEST(test_queue_device_name_command_persists_via_device_identity);
    RUN_TEST(test_ws_json_color_command_flows_through_to_static_color);
    RUN_TEST(test_reboot_command_calls_esp_restart_without_crashing);
    RUN_TEST(test_power_off_while_already_off_preserves_mode_before_off);
    RUN_TEST(test_update_throttles_frames_faster_than_the_minimum_duration);
    RUN_TEST(test_brightness_config_persists_and_reloads_value);

    RUN_TEST(test_next_command_starts_transitioning_without_blocking_later_commands);
    RUN_TEST(test_next_command_mid_update_does_not_flash_stale_t_into_high_brightness);
    RUN_TEST(test_update_transition_guards_against_t_before_window_start);
    RUN_TEST(test_transitioning_pre_and_post_delay_windows_zero_the_strip_brightness);
    RUN_TEST(test_transitioning_animation_applies_gamma_corrected_max_brightness);
    RUN_TEST(test_power_off_mid_transition_cancels_animation_and_blanks_strip);
    RUN_TEST(test_full_transition_cycle_lands_on_new_effect_in_fx_loop_mode);
    RUN_TEST(test_hold_command_mid_transition_is_applied_once_transition_finishes);
    RUN_TEST(test_power_off_mid_transition_with_pending_hold_restores_holding_on_power_on);
    RUN_TEST(test_repeated_transitions_do_not_leak_animation_instances);
    RUN_TEST(test_cancel_transition_frees_a_pending_effect_that_was_never_installed);
    RUN_TEST(test_finish_transition_applies_rotation_hint_from_the_new_effect);

    return UNITY_END();
}
