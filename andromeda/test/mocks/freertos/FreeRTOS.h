#pragma once

// Minimal FreeRTOS type/macro stand-ins for native unit tests.
// MissionControl only uses a handful of primitives (queues, tick delay) -
// this is not a general FreeRTOS shim.

#include <cstdint>

using BaseType_t = int;
using TickType_t = uint32_t;

constexpr BaseType_t pdTRUE = 1;
constexpr BaseType_t pdFALSE = 0;

inline TickType_t pdMS_TO_TICKS(uint32_t ms) { return ms; }

// No real scheduler on the host - a task delay is a no-op for tests.
inline void vTaskDelay(TickType_t) {}
