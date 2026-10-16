#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#if __has_include(<freertos/idf_additions.h>)
#include <freertos/idf_additions.h>
#define DEEP_DOG_HAS_TASK_CAPS 1
#else
#define DEEP_DOG_HAS_TASK_CAPS 0
#endif

#include <esp_heap_caps.h>

#include "face_ai_config.h"

/** 优先 PSRAM 栈，失败回退 internal。 */
inline bool DeepDogFaceTaskCreate(const char* name, TaskFunction_t fn, uint32_t stack_words, void* arg,
                                  UBaseType_t prio, TaskHandle_t* out) {
#if DEEP_DOG_HAS_TASK_CAPS && defined(CONFIG_SPIRAM)
    if (xTaskCreateWithCaps(fn, name, stack_words, arg, prio, out, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS) {
        return true;
    }
#endif
    return xTaskCreate(fn, name, stack_words, arg, prio, out) == pdPASS;
}

/**
 * MEM-08（S09）：Immich multipart / JPEG 等大缓冲在 PSRAM 分配失败后，是否还允许
 * 回退到 `MALLOC_CAP_INTERNAL`。internal 余量低于阈值时返回 false，调用方应直接
 * 跳过本次分配（打日志、让上层退避重试），而不是把 internal 连续块切碎。
 */
inline bool DeepDogInternalFallbackAllowed() {
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >= DEEP_DOG_FACE_INTERNAL_FALLBACK_MIN_FREE;
}
