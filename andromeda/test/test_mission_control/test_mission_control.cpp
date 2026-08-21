#include <unity.h>

// mission-control.cpp and effects.cpp are #included directly (not compiled
// via platformio.ini's build_src_filter) so this test binary stays fully
// self-contained: mission-control.cpp needs getRandomEffect() (effects.cpp)
// and getRandomAnimation() (stubbed below, see the comment further down) to
// even link, and effects.cpp's concrete effect classes are file-local so
// they must be reached this way. Since PlatformIO builds each test_xxx
// directory as its own separate program, this doesn't conflict with
// test_effects.cpp doing the same #include independently.
#include "../../src/effects.cpp"
#include "animation-base.h"
#include "ws-command-parser.h"

// AbstractAnimation::GetName()/run() are declared in animation-base.h but
// never defined anywhere in production code - every real animation
// overrides both, so this dead code path never needed a definition until
// now. StubAnimation (below) overrides them too, but the compiler still
// needs base-class vtable entries to exist at link time.
const char* AbstractAnimation::GetName() { return "AbstractAnimation"; }
void AbstractAnimation::run() {}

// Minimal animation stand-in so mission-control.cpp links without pulling in
// the real animations.cpp - which is out of scope for native tests (its
// animations drive real-time delay() busy loops, see the testing plan).
// AbstractAnimation::GetName()/run() are declared but never defined for the
// base class in production code (only overridden in concrete animations),
// so instantiating AbstractAnimation directly wouldn't link either way.
class StubAnimation : public AbstractAnimation
{
   public:
    const char* GetName() override { return "StubAnimation"; }
    void run() override {}
};
AbstractAnimation* getRandomAnimation() { return new StubAnimation(); }

#include "../../src/mission-control.cpp"

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
};

void setUp()
{
    GEOMETRY.initializeForTest(ModelId::SINGLE_STRIP_TEST_DEVICE);

    // In production, MissionControl::effect is only ever read after
    // handleTransition() has run at least once - guaranteed because
    // nextTransition defaults to 0, so the very first real update(millis())
    // call always takes the "t >= nextTransition" branch first. Tests call
    // the private setNextTransition()/calcBrightness() directly (bypassing
    // handleTransition()), which can leave that invariant broken for
    // whichever test runs next against the shared MissionControl singleton -
    // so each test starts from a known-initialized effect.
    MissionControlTestAccess::setEffect(MissionControl::Instance(), new StaticColor());
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

    mc.update(0);

    TEST_ASSERT_NOT_NULL(MissionControlTestAccess::getEffect(mc));
}

void test_power_off_and_on_toggle_state()
{
    MissionControl& mc = MissionControl::Instance();

    TEST_ASSERT_TRUE(mc.queueWebCommand(Command::PowerOff()));
    mc.update(0);  // ON is now false; update() should take the early-return path

    TEST_ASSERT_TRUE(mc.queueWebCommand(Command::PowerOn()));
    mc.update(0);  // processWebCommands() still runs even while OFF, turning it back ON
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

void test_queue_brightness_command_updates_max_brightness()
{
    MissionControl& mc = MissionControl::Instance();

    TEST_ASSERT_TRUE(mc.queueWebCommand(Command::Brightness(77)));
    mc.update(0);

    TEST_ASSERT_EQUAL_UINT8(77, mc.getMaxBrightness());
}

void test_queue_model_command_updates_factory_config()
{
    MissionControl& mc = MissionControl::Instance();

    TEST_ASSERT_TRUE(mc.queueWebCommand(Command::Model(static_cast<uint16_t>(ModelId::L70_MK1))));
    mc.update(0);

    TEST_ASSERT_EQUAL(ModelId::L70_MK1, FactoryConfig::getModelId());
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
    RUN_TEST(test_calc_brightness_ramps_up_during_fade_in);
    RUN_TEST(test_calc_brightness_is_max_during_hold);
    RUN_TEST(test_calc_brightness_ramps_down_during_fade_out);
    RUN_TEST(test_calc_brightness_after_transition_is_zero);

    RUN_TEST(test_set_next_transition_orders_timestamps_correctly);

    RUN_TEST(test_queue_web_command_and_process_hold);
    RUN_TEST(test_queue_web_command_fills_and_rejects_when_full);
    RUN_TEST(test_update_does_not_dereference_null_effect_before_first_transition);
    RUN_TEST(test_power_off_and_on_toggle_state);
    RUN_TEST(test_queue_color_command_sets_static_color_and_enters_static_mode);
    RUN_TEST(test_queue_color_command_while_already_in_static_mode_updates_color);
    RUN_TEST(test_queue_brightness_command_updates_max_brightness);
    RUN_TEST(test_queue_model_command_updates_factory_config);
    RUN_TEST(test_ws_json_color_command_flows_through_to_static_color);

    return UNITY_END();
}
