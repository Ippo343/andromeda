#pragma once

// Minimal stand-in for ESP-IDF's <freertos/semphr.h> on native (host) builds.
//
// Real mutex, not a spinlock: comms.cpp guards scanResults (written from the WiFi event
// callback, read from the web server task) with one, specifically because the guarded
// operation is a String copy, which allocates - doing that under a spinlock (which
// disables interrupts) risks a deadlock/abort against the heap allocator's own lock, the
// same class of bug the /scan critical section originally had. No real concurrency on the
// host - both "tasks" are just regular function calls on the same thread - so these are
// no-ops, matching the rest of FreeRTOS.h.

#include "FreeRTOS.h"

using SemaphoreHandle_t = void*;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return nullptr; }
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t) { return pdTRUE; }
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t) { return pdTRUE; }
constexpr TickType_t portMAX_DELAY = 0xFFFFFFFF;
