// Native unit tests for include/fs-health.h's mayServeFromFs()/markFsUnmountedForUpdate()
// pair - the boot-lifetime flag comms.cpp's static-file/log routes check before touching
// LittleFS, and OtaUpdater's updateTask() sets right before LittleFS.end() (src/ota-updater.cpp).
// Route-handler-level coverage (503 + no LittleFS access once the flag is set) lives in
// test_comms_integration.cpp, which can drive the real handlers via CommsTestAccess; this
// suite covers the pure flag itself.
#include <unity.h>

#include "fs-health.h"

void test_fs_health_setUp() { g_fsUnmountedForUpdate.store(false); }
void test_fs_health_tearDown() { g_fsUnmountedForUpdate.store(false); }

// Good weather: normal boot, nothing has unmounted LittleFS.
void test_may_serve_from_fs_is_true_by_default() { TEST_ASSERT_TRUE(mayServeFromFs()); }

// Bad weather: once the OTA filesystem write marks the flag, mayServeFromFs() must report
// false - and stay false, since a reboot always follows and there's no remount event to
// clear it for.
void test_may_serve_from_fs_is_false_after_marking_unmounted()
{
    markFsUnmountedForUpdate();
    TEST_ASSERT_FALSE(mayServeFromFs());

    // Never clears itself - a second, unrelated call site checking later in the same boot
    // must see the same answer.
    TEST_ASSERT_FALSE(mayServeFromFs());
}

void run_test_fs_health_tests()
{
    RUN_TEST(test_may_serve_from_fs_is_true_by_default);
    RUN_TEST(test_may_serve_from_fs_is_false_after_marking_unmounted);
}
