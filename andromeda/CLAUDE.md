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

Core pattern: singletons accessed globally. Main loop: `MissionControl::update(millis())` processes queued commands, renders effect/animation, manages timing/power.

## Design Principles

- **Decoupled:** Effects handle per-LED color; animations manage sequences; geometry holds device data. Independently testable.
- **Performance-first:** Precompute per-frame values; use flat arrays indexed by LED position (no dynamic structures in hot paths).
- **Device-agnostic:** Effects work with strip/LED indices; geometry provides coordinates for effects that need layout.
- **Multi-board:** Compile-time board variants + runtime model ID selection let same code run on all hardware.

## Core Subsystems

**MissionControl** — Main render/command loop. Receives queued web commands, orchestrates effect/animation render, manages power/brightness/CPU frequency/frame rate.

**Geometry** — Device config & LED data (strip count/lengths/pins, Cartesian/polar coordinates, constraints). Initialized with ModelId at startup.

**Effects** — Abstract: compute CRGB per LED per frame via `evaluate(strip, led, led_idx, t)`. Optional `precompute(t)` and `postprocess(t)` for optimization.

**Animations** — State machines managing effect selection & transitions. Decide which effect to render with what parameters.

**Comms** — WiFi + web server. HTTP endpoints queue commands (NEXT, HOLD, POWER_OFF, POWER_ON, COLOR) to MissionControl.

## Constraints

- **Time:** `milliseconds_t` (uint32_t); effects receive `t` for frame-relative math.
- **Memory:** LEDs in strips; coordinates in flat arrays. No per-LED allocation.
- **CPU:** Frequency scaling (80–240 MHz) + frame rate capping per device.
- **Brightness:** Global limiter each frame.

## Conventions

**Coordinates:** Cartesian (x,y in mm) or Polar (angle,distance) per device.  
**Building:** platformio.ini defines variants; all build by default; board-specific defines control conditional code.  
**Style:** C++17, singletons, virtual base classes, precompute over per-LED work, no allocations in hot paths.

## File Structure

Use glob/grep to explore live: `include/` (headers), `src/` (implementations), `geometry/` (model configs). Don't hardcode paths; search when needed.

## Testing

Native (host) unit tests run via `pio test -e native` — compiled and run on the dev machine's GCC toolchain, not on real hardware, using FastLED's own native/stub platform. Covers utils, geometry math, effects, MissionControl's pure logic, and perf-monitor. `comms.cpp` and `animations.cpp` are excluded (real-time/network-bound, no small seam exists). `test/mocks/` provides minimal Arduino/ArduinoLog/Preferences/FreeRTOS stand-ins for the native build; `-DUNIT_TEST` gates a few test-only `friend` accessors into otherwise-private class internals. Requires a host GCC toolchain on `PATH` (MSYS2 on Windows — not bundled by PlatformIO). CI runs this plus a build-only check of all 3 hardware envs, see `.github/workflows/test.yml`.
