#pragma once

// Minimal, genuinely-functional FreeRTOS queue stand-in for native unit
// tests: a small fixed-capacity ring buffer of fixed-size items. Real
// enough to exercise MissionControl::queueWebCommand()/processWebCommands()
// (full/empty behavior) without pulling in the real FreeRTOS kernel.

#include <cstdint>
#include <cstring>
#include <vector>

#include "FreeRTOS.h"

struct SimpleQueue
{
    size_t itemSize;
    size_t capacity;
    std::vector<uint8_t> storage;
    size_t head = 0;   // next slot to read
    size_t count = 0;  // number of items currently stored

    SimpleQueue(size_t length, size_t itemSize_)
        : itemSize(itemSize_), capacity(length), storage(length * itemSize_)
    {
    }
};

using QueueHandle_t = SimpleQueue*;

inline QueueHandle_t xQueueCreate(size_t uxQueueLength, size_t uxItemSize)
{
    return new SimpleQueue(uxQueueLength, uxItemSize);
}

inline BaseType_t xQueueSend(QueueHandle_t queue, const void* item, TickType_t)
{
    if (!queue || queue->count >= queue->capacity) return pdFALSE;
    size_t tail = (queue->head + queue->count) % queue->capacity;
    std::memcpy(&queue->storage[tail * queue->itemSize], item, queue->itemSize);
    queue->count++;
    return pdTRUE;
}

inline BaseType_t xQueueReceive(QueueHandle_t queue, void* outItem, TickType_t)
{
    if (!queue || queue->count == 0) return pdFALSE;
    std::memcpy(outItem, &queue->storage[queue->head * queue->itemSize], queue->itemSize);
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;
    return pdTRUE;
}
