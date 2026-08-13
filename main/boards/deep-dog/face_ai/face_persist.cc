#include "face_persist.h"

#include "face_ai_config.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#if DEEP_DOG_FACE_AI_ENABLE && defined(CONFIG_IDF_TARGET_ESP32S3)

#define TAG "face_persist"

namespace {

struct PersistJob {
    bool sync;
};

QueueHandle_t s_queue = nullptr;
TaskHandle_t s_task = nullptr;
SemaphoreHandle_t s_done = nullptr;
esp_err_t s_last_err = ESP_OK;
bool s_ready = false;

void PersistTask(void* /*arg*/) {
    PersistJob job{};
    for (;;) {
        if (xQueueReceive(s_queue, &job, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        // 合并队列里积压的 async 请求，只写一次
        PersistJob peek{};
        while (xQueueReceive(s_queue, &peek, 0) == pdTRUE) {
            if (peek.sync) {
                job.sync = true;
            }
        }
        s_last_err = DeepDogFaceRecognizeSaveMetaToNvs();
        if (job.sync && s_done) {
            xSemaphoreGive(s_done);
        }
    }
}

}  // namespace

bool DeepDogFacePersistInit() {
    if (s_ready) {
        return true;
    }
    s_queue = xQueueCreate(4, sizeof(PersistJob));
    s_done = xSemaphoreCreateBinary();
    if (!s_queue || !s_done) {
        DeepDogFacePersistShutdown();
        return false;
    }
    if (xTaskCreate(PersistTask, "face_persist", DEEP_DOG_FACE_PERSIST_TASK_STACK, nullptr, 4, &s_task) != pdPASS) {
        ESP_LOGW(TAG, "face_persist task create failed");
        DeepDogFacePersistShutdown();
        return false;
    }
    s_ready = true;
    ESP_LOGI(TAG, "persist worker ready (stack=%u internal)", (unsigned)DEEP_DOG_FACE_PERSIST_TASK_STACK);
    return true;
}

void DeepDogFacePersistShutdown() {
    if (s_task) {
        vTaskDelete(s_task);
        s_task = nullptr;
    }
    if (s_queue) {
        vQueueDelete(s_queue);
        s_queue = nullptr;
    }
    if (s_done) {
        vSemaphoreDelete(s_done);
        s_done = nullptr;
    }
    s_ready = false;
}

bool DeepDogFacePersistIsReady() {
    return s_ready;
}

void DeepDogFacePersistFlushAsync() {
    if (!s_ready || !s_queue) {
        (void)DeepDogFaceRecognizeSaveMetaToNvs();
        return;
    }
    const PersistJob job{false};
    (void)xQueueSend(s_queue, &job, 0);
}

bool DeepDogFacePersistFlushSync() {
    if (!s_ready || !s_queue || !s_done) {
        return DeepDogFaceRecognizeSaveMetaToNvs() == ESP_OK;
    }
    xSemaphoreTake(s_done, 0);
    const PersistJob job{true};
    if (xQueueSend(s_queue, &job, pdMS_TO_TICKS(3000)) != pdTRUE) {
        return false;
    }
    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGW(TAG, "persist sync timeout");
        return false;
    }
    return s_last_err == ESP_OK;
}

#else

bool DeepDogFacePersistInit() {
    return true;
}
void DeepDogFacePersistShutdown() {}
bool DeepDogFacePersistIsReady() {
    return false;
}
void DeepDogFacePersistFlushAsync() {}
bool DeepDogFacePersistFlushSync() {
    return true;
}
esp_err_t DeepDogFaceRecognizeSaveMetaToNvs() {
    return ESP_OK;
}

#endif
