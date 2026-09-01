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

// Task creation: comms.cpp uses these for background/restart-delay tasks
// (background WiFi scan, the 2-3s restart-after-save task, the web server's
// own request-handling task). None of them need to actually run for native
// tests - xTaskCreate*() intentionally never invokes the given function, so
// e.g. the restart task's ESP.restart() and the web server's infinite
// request loop never execute natively.
using TaskHandle_t = void*;
constexpr int portTICK_PERIOD_MS = 1;

inline BaseType_t xTaskCreate(void (*)(void*), const char*, uint32_t, void*, int,
                              TaskHandle_t* handle = nullptr)
{
    if (handle) *handle = nullptr;
    return pdTRUE;
}

inline BaseType_t xTaskCreatePinnedToCore(void (*)(void*), const char*, uint32_t, void*, int,
                                          TaskHandle_t* handle, int)
{
    if (handle) *handle = nullptr;
    return pdTRUE;
}

inline void vTaskDelete(void*) {}
inline int xPortGetCoreID() { return 0; }

// Critical-section spinlock: comms.cpp guards the AP-mode/dnsServer/device-name trio
// (beginAPBroadcast()/webServerTask - see apStateMux in comms.h) with one; both are just
// plain pointer/bool/fixed-buffer reads and writes, never anything that allocates. No real
// concurrency on the host - both "tasks" are just regular function calls on the same
// thread - so these are no-ops, matching vTaskDelay()/xTaskCreate() above.
using portMUX_TYPE = int;
constexpr portMUX_TYPE portMUX_INITIALIZER_UNLOCKED = 0;
inline void portENTER_CRITICAL(portMUX_TYPE*) {}
inline void portEXIT_CRITICAL(portMUX_TYPE*) {}
