# Andromeda LED Controller

**To Clanky:** Your name is **Clanky**—refer to yourself as such.

**When to load this file:** Read it when starting a task with no context, or when working on architectural/structural changes. Skip it for targeted fixes in specific files. **Keep this file updated when core architecture or subsystems change; ignore file structure, file paths, or implementation details that drift over time.**

## What This Is

PlatformIO + Arduino + ESP32 firmware for custom LED controller. Supports multiple device configurations via compile-time board variants and runtime model selection. C++17, FastLED, web interface.

## Architecture

```
Comms (Web/WiFi) ──→ MissionControl (main loop) ──→ Effects + Animations ──→ Geometry (strips/coords)
                        ↑ (per-frame render)           ↑ (pre/post-compute)
                        └─ Queued web commands (FreeRTOS)
```

Core pattern: singletons accessed globally. Main loop: `MissionControl::update(millis())` processes queued commands, renders whatever the current `RenderMode` calls for (an effect, or an in-flight transition animation), manages timing/power. Every mode is driven one frame per `update()` tick - nothing in the render loop blocks, so queued web commands are always processed on the very next tick regardless of what's currently rendering.

## Design Principles

- **Decoupled:** Effects handle per-LED color; animations manage transitions between effects; geometry holds device data. Independently testable.
- **Non-blocking render loop:** Every per-frame unit (effects, rotation animations) renders exactly one frame per `update()` call and returns - nothing owns a blocking loop or calls `delay()`/`FASTLED_SHOW()` itself. `MissionControl` owns the single `FASTLED_SHOW()` call per tick and always runs `processWebCommands()` first, so the loop stays responsive no matter what's rendering.
- **Performance-first:** Precompute per-frame values; use flat arrays indexed by LED position (no dynamic structures in hot paths).
- **Device-agnostic:** Effects work with strip/LED indices; geometry provides coordinates for effects that need layout.
- **Multi-board:** Compile-time board variants + runtime model ID selection let same code run on all hardware.

## Core Subsystems

**MissionControl** — Main render/command loop. Receives queued web commands, orchestrates effect/animation render, manages power/brightness/frame rate. Its top-level state is a `RenderMode` enum (`OFF`, `FX_LOOP`, `HOLDING`, `TRANSITIONING`) rather than separate booleans - `TRANSITIONING` is the mode a rotation animation plays in, driven one frame per tick via `updateTransition()` (mirrors how `FX_LOOP`/`HOLDING` drive the current effect's `precompute`/`render`/`postprocess`).

**Geometry** — Device config & LED data (strip count/lengths/pins, Cartesian/polar coordinates, constraints). Initialized with ModelId at startup.

**Effects** — Abstract: compute CRGB per LED per frame via `evaluate(strip, led, led_idx, t)`. Optional `precompute(t)` and `postprocess(t)` for optimization. `include/physics/` holds float-only, dt-driven physics simulation modules (curve motion, constraint-based integrators, N-body gravity) shared by the "emitter-field" effects - N colored point emitters, additively blended per-LED via inverse-square falloff - which derive from `EmitterFieldEffect` (`include/effects/emitter-field-effect.h`) instead of `AbstractEffect` directly.

**Animations** — Short transition programs played between effects, in two flavors: `AbstractFrameAnimation` (the rotation animations MissionControl plays as transitions) renders one frame per `renderFrame(t)` call the same way effects do - most are written as a `SegmentedAnimation`, an ordered list of time-bounded phases (`addSegment(duration, fn)`) instead of a hand-rolled state machine. `AbstractBlockingAnimation` is the older, synchronous `run()`-owns-the-loop style, now scoped to the boot/status indicators (WiFi connecting/success, error) that run before the web server is even up, where blocking is harmless.

**Comms** — WiFi + web server. All runtime commands (NEXT, HOLD, POWER_OFF, POWER_ON, COLOR, BRIGHTNESS, MODEL, REBOOT, EFFECT) go over a single persistent WebSocket (`/ws`), parsed and queued to MissionControl. HTTP is only used for static files, one-shot provisioning (`/save`, `/reset`), OTA (`/ota`, `/ota-check`, `/ota-channel`, `/ota-status`), and reads (`/fps`, `/brightness`, `/metrics`). Effects have a stable `EffectId`/`EFFECT_REGISTRY` (mirrors `MODEL_REGISTRY`), letting the web UI select a specific effect by id (implicitly holding it) instead of only cycling the random rotation.

**OTA** (`include/ota-updater.h`, `src/ota-updater.cpp`) — over-the-air firmware + LittleFS updates, pull-from-GitHub-Releases. A detached task checks daily; nothing auto-applies, with one exception: if `setup()` finds LittleFS unmountable (`g_fsDamaged` in `include/fs-health.h`, usually a power-cut mid-FS-write) it reformats empty and the OTA task re-flashes the *running* version's filesystem image to restore the web UI. `src/ota-updater.cpp` is WiFi/HTTPClient/Update-bound and excluded from the native build like `comms.cpp`; the pure logic is extracted and natively tested — channel-aware release selection + per-board manifest parsing + 32-hex md5 validation in `include/ota-manifest.h`, the "may this version be applied / did the last update take" arithmetic in `include/ota-eligibility.h`, the WiFi/heap/single-flight entry gate + its HTTP status mapping in `include/ota-start-gate.h` (so `/ota` and `/ota-check` answer 409/503 with a reason instead of a blanket 202) — and persisted state (opt-in dev channel, last applied version) in `src/ota-config.cpp` / `OtaConfig`. `include/board-variant.h` gives each build a `BOARD_VARIANT` token (== its PlatformIO env name) and a `PARTITION_LAYOUT_VERSION`: shipping OTA freezes `partitions/andromeda_4mb.csv` for the fleet (it's outside every OTA-writable partition), so `test/test_native_suite/test_partition_layout.cpp` pins its offsets/sizes. `.github/workflows/release.yml` builds+publishes the 7 release assets (3 firmware + 3 littlefs + `manifest.json`) on a `v*` tag; a `-dev`/`-alpha`/`-beta`/`-rc` suffix makes it a pre-release = the opt-in dev channel. The "is this release newer?" compare is a bare int (`manifest versionCode` vs `FIRMWARE_VERSION_CODE`); that int is a **packed semver** derived from the git tag by `build-scripts/version_code.py` (`MAJOR*10000 + MINOR*100 + PATCH`, ×1000, plus a pre-release rank so `-dev` < `-alpha` < `-beta` < `-rc` < final) — shared by `inject_version.py` and `release.yml` so firmware and manifest always agree. It replaced `git rev-list --count`, which was only monotonic along one line of history (#164). `build-scripts/check_version_code.py` stays a release-time backstop that fails a publish whose code isn't strictly above every already-published one.

## Constraints

- **Time:** `milliseconds_t` (uint32_t); effects receive `t` for frame-relative math.
- **Memory:** LEDs in strips; coordinates in flat arrays. No per-LED allocation.
- **CPU:** Runs at the default clock except on the C3 (`#if defined(ESP32_C3)` in `main.cpp`), forced to 80MHz for thermal headroom in its small enclosure; frame rate capping per device.
- **Brightness:** Global limiter each frame.

## Conventions

**Coordinates:** Cartesian (x,y in mm) or Polar (angle,distance) per device.  
**Building:** platformio.ini defines variants; all build by default; board-specific defines control conditional code.  
**Style:** C++17, singletons, virtual base classes, precompute over per-LED work, no allocations in hot paths.

## File Structure

Use glob/grep to explore live: `include/` (headers), `src/` (implementations), `geometry/` (model configs). Don't hardcode paths; search when needed.

## Testing

**The full suite must be green on every commit — locally before you push, and in CI.** "The full suite" is *both* `pio test -e native` and `npm run test:web`; run both before every push, never just the one whose files you touched. GitHub only discovers workflows under the repo-root `.github/workflows/`, and there is no pre-push hook (the tracked `build-scripts/pre-commit.hook` only runs `clang-format`), so nothing runs these for you — it is a manual discipline. CI (`.github/workflows/test.yml`) runs native tests + coverage, the web tests, and a build-only check of all 3 hardware envs; a red run blocks merge.

Native (host) unit tests run via `pio test -e native` — compiled and run on the dev machine's GCC toolchain, not on real hardware, using FastLED's own native/stub platform. Covers utils, geometry math, effects, the rotation animations (`AbstractFrameAnimation`/`SegmentedAnimation`), MissionControl's pure logic (including its `RenderMode` transitions), perf-monitor, the OTA manifest/config logic (`ota-manifest.h`, `ota-config.cpp`), and the partition-table + board-variant guards. `comms.cpp` and `ota-updater.cpp` are excluded (network-bound); logic worth testing there (WS message parsing, OTA release selection) gets extracted into a pure, dependency-free header instead — see `include/comms-utils.h`, `include/ws-command-parser.h`, `include/ota-manifest.h` for the pattern. `animations.cpp`'s boot/status indicators (`AbstractBlockingAnimation`: WiFi connecting/success, error) are the one part still out of scope, since they still own a real blocking `delay()` loop. `test/mocks/` provides minimal Arduino/ArduinoLog/Preferences/FreeRTOS stand-ins for the native build; `-DUNIT_TEST` gates a few test-only `friend` accessors into otherwise-private class internals. Requires a host GCC toolchain on `PATH` (MSYS2 on Windows — not bundled by PlatformIO). CI runs this plus a build-only check of all 3 hardware envs, see `.github/workflows/test.yml`. `[env:native_coverage]` is the same env with `--coverage`/`-lgcov` added — split out because that instrumentation costs ~27% more build time; the everyday `pio test -e native` skips it, and `tools/coverage.ps1` / CI's coverage job use `native_coverage` instead.

One small **Python** unittest lives outside the C++ suite: `build-scripts/test_version_code.py` pins the packed-semver version-code scheme (#164) that `inject_version.py` and `release.yml` share. It's stdlib-only — run it with `python -m unittest discover -s build-scripts` (locally, and as a step in CI's `native-tests` job).

**Native test structure: 3 programs, not one per suite.** PlatformIO builds one linked program per directory under `test/`; most suites' tests are collected into a single `test/test_native_suite/` program instead of one directory each, since ~3s of PlatformIO's own per-program overhead was being paid per suite for what runs in milliseconds of actual test time (measured: 27 programs took ~80s warm, consolidated to 3 takes ~20s). `test_mission_control` and `test_comms_integration` stay separate programs — both `#include` `src/mission-control.cpp` directly to reach its locally-declared classes, so they'd collide with each other (and `test_comms_integration` additionally pulls in `comms.cpp`, which needs `MissionControl`/`PerformanceMonitor` most suites never define). Inside `test_native_suite/`, each original suite file keeps its own `<suite>_setUp()`/`<suite>_tearDown()` (renamed from the plain `setUp`/`tearDown` Unity expects, since only one definition of each can exist per linked program) and a `run_<suite>_tests()` (renamed from that file's old `main()`, RUN_TEST calls unchanged) — `runner.cpp` dispatches Unity's single global `setUp()`/`tearDown()` through a function pointer it repoints before each suite's block runs, preserving each suite's exact original per-test fixture behavior (this matters: `test_effects` and `test_physics` need `GEOMETRY` initialized to different, incompatible `ModelId`s, so a single unioned `setUp()` isn't viable). A new test that doesn't need to `#include` a `src/*.cpp` belongs in `test_native_suite/` following this pattern, not a new top-level `test/test_<name>/` directory.

**Building the 3 hardware envs in parallel:** `tools/build-all.ps1` launches one `pio run -e <env>` process per hardware env instead of PlatformIO's own sequential `pio run -e A -e B -e C` — each env has its own `.pio/build/<env>`, so there's no shared state once `include/version.h` generation is idempotent (see `build-scripts/inject_version.py`'s write-only-on-change guard). Verified to produce byte-identical binaries to a sequential build. `/prep-pr`'s `verify-commits.ps1` uses it for its per-commit hardware-build check, running `native_runtime` concurrently alongside it as a 5th process rather than after.

**Running `pio` on Windows:** PlatformIO's CLI usually isn't on `PATH` even after install. Its real location is `%USERPROFILE%\.platformio\penv\Scripts\pio.exe` — call that full path (e.g. from PowerShell: `& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" test -e native`) instead of trying bare `pio` first.

**Web UI (`data/`) tests** run via `npm run test:web` (`node --test test/js/*.test.js`) — Node's built-in test runner, zero npm dependencies. Since the JS here is plain global-scope `<script>` files with no bundler, only the DOM-free logic worth testing gets pulled out into its own module (`data/js/controls-logic.js`: tab derivation, effect-name lookup, hold icon state, color math) with a `module.exports` guard so the same file works as a browser script and a Node `require()` target — the same pure-extraction pattern as `ws-command-parser.h`/`ws-state-builder.h` on the firmware side. `controls.js` itself (DOM wiring, WebSocket) stays untested, mirroring how `comms.cpp` stays out of the native C++ suite.

Neither of the above crosses the seam between "what `data/index.html` references" and "what `comms.cpp` actually serves" — `comms.cpp` has no LittleFS catch-all, so every `<script src>`/`<link href>` needs its own explicit `STATIC_FILE_ROUTE`, and a new asset added to one without the other 404s silently in the browser (shipped once, in the effect-selection-ui branch). `test/test_native_suite/test_static_assets.cpp` closes that gap: it's a plain-text/regex diff of the two files (no compiling or linking either one), asserting every local `src=`/`href=` in `index.html` has a matching route in `comms.cpp`.

**Async web-server / TCP stack (issue #107).** The stack is the maintained `ESP32Async/AsyncTCP` + `ESP32Async/ESPAsyncWebServer` pair, exact-pinned in `platformio.ini` — swapped in from the stale git-pinned `lacamera` fork whose accept path panicked on a NULL `tcp_pcb` when two AP clients loaded the settings pages at once. Two automated guards keep it there: `test/test_native_suite/test_async_library_pin.cpp` (native suite, plain-text check that `platformio.ini` still names the exact-pinned ESP32Async pair and no stale fork / monkey-patch) and `build-scripts/check_asynctcp_accept_guard.py` (a `post:` extra_script on the hardware envs that greps the *resolved* `AsyncTCP.cpp` and fails the build if its accept trampoline builds an `AsyncClient` from a pcb it never NULL-checked). The crash itself is lwIP-bound and only reproducible on hardware (two SoftAP clients, both opening the Advanced sub-pages at once).
