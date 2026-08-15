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
