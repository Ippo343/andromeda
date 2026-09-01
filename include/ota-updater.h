#pragma once

#include <cstddef>
#include <cstdint>

// Over-the-air firmware + filesystem updates (#63). Pulls a release manifest
// from GitHub, compares it against FIRMWARE_VERSION_CODE, and - on request -
// downloads and flashes the inactive OTA slot, then reboots.
//
// All network/flash work runs on short-lived FreeRTOS worker tasks (like
// Comms' saveWorkerTask), never on the render loop or the async_tcp handler.
// Callers only ever kick a task and poll status(); this header has no
// Arduino/networking types so comms.cpp can include it freely.

namespace OtaUpdater
{

enum class State : uint8_t
{
    Idle,             // nothing has happened yet this boot
    Checking,         // querying GitHub for the latest release
    UpToDate,         // checked: this board is already on the newest build
    UpdateAvailable,  // checked: a newer build exists for this board+channel
    Downloading,      // update in progress: fetching
    WritingFw,        // update in progress: writing the firmware slot
    WritingFs,        // update in progress: writing the filesystem
    Rebooting,        // update done: about to restart into the new build
    Failed,           // last check or update errored (see Status::error)
};

// Snapshot for GET /ota-status and the /metrics fields. Returned by value
// under a lock, so the caller never sees a half-updated struct.
struct Status
{
    State state;
    uint8_t progressPct;         // 0-100, meaningful while Downloading/Writing*
    uint32_t latestVersionCode;  // from the last successful check (0 if none)
    char latestTag[48];          // ""  if no check has succeeded
    char error[96];              // ""  unless state == Failed
};

// Create the status lock. Call once from setup() before any task is spawned.
void begin();

// Spawn a background check against GitHub (channel from OtaConfig). No-op if a
// check/update is already running or WiFi isn't connected in station mode.
void startCheck();

// Spawn the background download + flash. No-op unless idle/checked and WiFi is
// up. Re-checks GitHub first (download URLs are short-lived), so it's safe to
// call without a prior startCheck(); reports UpToDate if nothing is newer.
void startUpdate();

// Thread-safe snapshot of the current state.
Status status();

// Convenience for /metrics: true iff the last check found a newer build.
bool updateAvailable();

}  // namespace OtaUpdater
