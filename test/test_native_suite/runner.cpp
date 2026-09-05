// test_native_suite: the merged program for the "plain" native test suites (the
// ones that don't #include a src/*.cpp - test_mission_control and
// test_comms_integration both pull in mission-control.cpp and would collide with
// each other and with anything else, so they stay separate programs).
//
// Consolidating from 27 -> 3 test programs removes PlatformIO's ~3s per-program
// overhead paid 27 times for what is otherwise ~30ms of actual test execution
// (measured - see the-build-times-are-jazzy-orbit.md). Each original suite kept
// its own setUp()/tearDown() (they differ - test_effects and test_physics need
// GEOMETRY initialized to different, incompatible ModelIds - so a single unioned
// setUp() across all suites is not viable); this runner instead dispatches Unity's
// single global setUp()/tearDown() through a function pointer that's repointed to
// each suite's own <suite>_setUp/<suite>_tearDown right before that suite's block
// of RUN_TEST calls runs. Every suite's own test bodies are untouched - only each
// file's `void setUp()`/`void tearDown()`/`int main()` were mechanically renamed to
// `void <suite>_setUp()`/`void <suite>_tearDown()`/`void run_<suite>_tests()`.
#include <unity.h>

static void (*g_setUp)() = nullptr;
static void (*g_tearDown)() = nullptr;

void setUp()
{
    if (g_setUp) g_setUp();
}
void tearDown()
{
    if (g_tearDown) g_tearDown();
}

#define DECLARE_SUITE(name)        \
    extern void name##_setUp();    \
    extern void name##_tearDown(); \
    extern void run_##name##_tests()

#define RUN_SUITE(name)               \
    do {                              \
        g_setUp = name##_setUp;       \
        g_tearDown = name##_tearDown; \
        run_##name##_tests();         \
    } while (0)

DECLARE_SUITE(test_animations);
DECLARE_SUITE(test_async_library_pin);
DECLARE_SUITE(test_board_variant);
DECLARE_SUITE(test_brightness_ceiling);
DECLARE_SUITE(test_comms);
DECLARE_SUITE(test_device_identity);
DECLARE_SUITE(test_device_uid);
DECLARE_SUITE(test_effects);
DECLARE_SUITE(test_fs_health);
DECLARE_SUITE(test_geometry);
DECLARE_SUITE(test_installer_model_list);
DECLARE_SUITE(test_log_suspend);
DECLARE_SUITE(test_log_writer_lock);
DECLARE_SUITE(test_mdns_hosts);
DECLARE_SUITE(test_ota_config);
DECLARE_SUITE(test_ota_eligibility);
DECLARE_SUITE(test_ota_manifest);
DECLARE_SUITE(test_ota_retry_schedule);
DECLARE_SUITE(test_ota_start_gate);
DECLARE_SUITE(test_partition_layout);
DECLARE_SUITE(test_perf_monitor);
DECLARE_SUITE(test_physics);
DECLARE_SUITE(test_power_monitor);
DECLARE_SUITE(test_release_pipeline);
DECLARE_SUITE(test_segmented_animation);
DECLARE_SUITE(test_serial_line_buffer);
DECLARE_SUITE(test_static_assets);
DECLARE_SUITE(test_utils);
DECLARE_SUITE(test_web_installer);
DECLARE_SUITE(test_wifi_recovery);
DECLARE_SUITE(test_ws_command_parser);
DECLARE_SUITE(test_ws_metrics_builder);
DECLARE_SUITE(test_ws_state_builder);

int main(int, char**)
{
    UNITY_BEGIN();

    RUN_SUITE(test_animations);
    RUN_SUITE(test_async_library_pin);
    RUN_SUITE(test_board_variant);
    RUN_SUITE(test_brightness_ceiling);
    RUN_SUITE(test_comms);
    RUN_SUITE(test_device_identity);
    RUN_SUITE(test_device_uid);
    RUN_SUITE(test_effects);
    RUN_SUITE(test_fs_health);
    RUN_SUITE(test_geometry);
    RUN_SUITE(test_installer_model_list);
    RUN_SUITE(test_log_suspend);
    RUN_SUITE(test_log_writer_lock);
    RUN_SUITE(test_mdns_hosts);
    RUN_SUITE(test_ota_config);
    RUN_SUITE(test_ota_eligibility);
    RUN_SUITE(test_ota_manifest);
    RUN_SUITE(test_ota_retry_schedule);
    RUN_SUITE(test_ota_start_gate);
    RUN_SUITE(test_partition_layout);
    RUN_SUITE(test_perf_monitor);
    RUN_SUITE(test_physics);
    RUN_SUITE(test_power_monitor);
    RUN_SUITE(test_release_pipeline);
    RUN_SUITE(test_segmented_animation);
    RUN_SUITE(test_serial_line_buffer);
    RUN_SUITE(test_static_assets);
    RUN_SUITE(test_utils);
    RUN_SUITE(test_web_installer);
    RUN_SUITE(test_wifi_recovery);
    RUN_SUITE(test_ws_command_parser);
    RUN_SUITE(test_ws_metrics_builder);
    RUN_SUITE(test_ws_state_builder);

    return UNITY_END();
}
