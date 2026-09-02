// Native unit tests for include/status-led-state.h - the pure state->colour
// mapping behind the on-board status LED (#141). The RMT bit-banging in
// status-led.h stays out of the native suite (Arduino/esp32-hal-rmt), same
// split as comms.cpp vs comms-utils.h.
#include <unity.h>

#include "status-led-state.h"

using StatusLed::colorFor;
using StatusLed::DeviceState;
using StatusLed::LedColor;
using StatusLed::shouldWrite;

void setUp() {}
void tearDown() {}

namespace
{
const DeviceState kRealStates[] = {
    DeviceState::Booting, DeviceState::WifiConnecting, DeviceState::WifiConnected,
    DeviceState::ApMode,  DeviceState::Error,          DeviceState::Running,
};
constexpr int kN = sizeof(kRealStates) / sizeof(kRealStates[0]);
}  // namespace

// Good weather: a real state change asks for a write, and every state has a
// distinct, non-black colour so the enum can't silently collide.
void test_state_change_triggers_a_write()
{
    TEST_ASSERT_TRUE(shouldWrite(DeviceState::WifiConnecting, DeviceState::WifiConnected));
    TEST_ASSERT_TRUE(shouldWrite(DeviceState::Booting, DeviceState::Running));
    TEST_ASSERT_TRUE(shouldWrite(DeviceState::Error, DeviceState::ApMode));
}

void test_every_state_has_a_distinct_non_black_colour()
{
    for (int i = 0; i < kN; i++)
    {
        LedColor c = colorFor(kRealStates[i]);
        TEST_ASSERT_TRUE_MESSAGE(c != (LedColor{0, 0, 0}), "a state maps to LED-off");
        for (int j = i + 1; j < kN; j++)
        {
            TEST_ASSERT_TRUE_MESSAGE(colorFor(kRealStates[i]) != colorFor(kRealStates[j]),
                                     "two states share a colour");
        }
    }
}

void test_booting_colour_matches_the_legacy_status_led()
{
    // The original statusLedOn() latched {5, 6, 3} (G, R, B); statusLedOn() is
    // now statusLedSet(Booting), so that mustn't change what a bare
    // powered-on board shows.
    TEST_ASSERT_TRUE(colorFor(DeviceState::Booting) == (LedColor{5, 6, 3}));
}

// Bad weather: no write when the state hasn't changed - so calling
// statusLedSet() repeatedly (e.g. per FX_LOOP frame) costs one comparison,
// not a 24-bit blocking RMT bit-bang.
void test_same_state_does_not_trigger_a_write()
{
    for (int i = 0; i < kN; i++) TEST_ASSERT_FALSE(shouldWrite(kRealStates[i], kRealStates[i]));
}

// Bad weather: a DeviceState cast from an out-of-range integer maps to the
// Error colour, not an index past the switch table.
void test_out_of_range_state_falls_back_to_error()
{
    LedColor err = colorFor(DeviceState::Error);
    TEST_ASSERT_TRUE(colorFor(static_cast<DeviceState>(200)) == err);
    TEST_ASSERT_TRUE(colorFor(DeviceState::_Count) == err);  // the sentinel maps to Error too
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_state_change_triggers_a_write);
    RUN_TEST(test_every_state_has_a_distinct_non_black_colour);
    RUN_TEST(test_booting_colour_matches_the_legacy_status_led);
    RUN_TEST(test_same_state_does_not_trigger_a_write);
    RUN_TEST(test_out_of_range_state_falls_back_to_error);
    return UNITY_END();
}
