#include "face_facedb.h"

#include "face_ai_config.h"

#include "dl_tensor_base.hpp"

#include <atomic>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#if DEEP_DOG_FACE_AI_ENABLE && DEEP_DOG_FACE_RECOG_ENABLE && defined(CONFIG_IDF_TARGET_ESP32S3)

#define TAG "face_facedb"

namespace {

enum class Op : uint8_t { EnrollFeat, DeleteFeat, ClearAll };

struct Job {
    Op op = Op::EnrollFeat;
    dl::TensorBase* feat = nullptr;
    uint16_t delete_id = 0;
    esp_err_t result = ESP_FAIL;
    SemaphoreHandle_t done = nullptr;
};

QueueHandle_t s_queue = nullptr;
TaskHandle_t s_task = nullptr;
bool s_ready = false;
std::atomic<bool> s_worker_busy{false};
std::atomic<bool> s_quiesce{false};

void RunJob(Job* job) {
    switch (job->op) {
        case Op::EnrollFeat:
            job->result = DeepDogFaceRecognizeFacedbEnrollFeat(job->feat);
            break;
        case Op::DeleteFeat:
            job->result = DeepDogFaceRecognizeFacedbDeleteFeat(job->delete_id);
            break;
        case Op::ClearAll:
            job->result = DeepDogFaceRecognizeFacedbClearAllFeats();
            break;
        default:
            job->result = ESP_ERR_INVALID_ARG;
            break;
    }
}

bool SubmitSync(Job* job, TickType_t wait_ticks) {
    if (!s_queue || !job || !job->done) {
        return false;
    }
    if (xQueueSend(s_queue, job, wait_ticks) != pdTRUE) {
        ESP_LOGW(TAG, "facedb queue full op=%u", static_cast<unsigned>(job->op));
        return false;
    }
    if (xSemaphoreTake(job->done, pdMS_TO_TICKS(15000)) != pdTRUE) {
        ESP_LOGW(TAG, "facedb sync timeout op=%u", static_cast<unsigned>(job->op));
        return false;
    }
    return true;
}

void FacedbTask(void* /*arg*/) {
    Job job{};
    for (;;) {
        if (xQueueReceive(s_queue, &job, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        s_worker_busy.store(true, std::memory_order_release);
        RunJob(&job);
        s_worker_busy.store(false, std::memory_order_release);
        if (job.done) {
            xSemaphoreGive(job.done);
        }
    }
}

esp_err_t RunSyncOp(Op op, dl::TensorBase* feat, uint16_t delete_id) {
    if (s_quiesce.load(std::memory_order_acquire) && op != Op::ClearAll) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_ready || !s_queue) {
        switch (op) {
            case Op::EnrollFeat:
                return DeepDogFaceRecognizeFacedbEnrollFeat(feat);
            case Op::DeleteFeat:
                return DeepDogFaceRecognizeFacedbDeleteFeat(delete_id);
            case Op::ClearAll:
                return DeepDogFaceRecognizeFacedbClearAllFeats();
        }
        return ESP_ERR_INVALID_STATE;
    }
    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    if (!done) {
        return ESP_ERR_NO_MEM;
    }
    Job job{};
    job.op = op;
    job.feat = feat;
    job.delete_id = delete_id;
    job.done = done;
    const bool ok = SubmitSync(&job, pdMS_TO_TICKS(3000));
    vSemaphoreDelete(done);
    return ok ? job.result : ESP_ERR_TIMEOUT;
}

}  // namespace

bool DeepDogFaceFacedbInit() {
    if (s_ready) {
        return true;
    }
    s_queue = xQueueCreate(2, sizeof(Job));
    if (!s_queue) {
        return false;
    }
    if (xTaskCreate(FacedbTask, "face_facedb", DEEP_DOG_FACE_FACEDB_TASK_STACK, nullptr, 4, &s_task) != pdPASS) {
        ESP_LOGW(TAG, "face_facedb task create failed stack=%u", (unsigned)DEEP_DOG_FACE_FACEDB_TASK_STACK);
        vQueueDelete(s_queue);
        s_queue = nullptr;
        return false;
    }
    s_ready = true;
    ESP_LOGI(TAG, "facedb worker ready (stack=%u internal)", (unsigned)DEEP_DOG_FACE_FACEDB_TASK_STACK);
    return true;
}

void DeepDogFaceFacedbShutdown() {
    if (s_task) {
        vTaskDelete(s_task);
        s_task = nullptr;
    }
    if (s_queue) {
        vQueueDelete(s_queue);
        s_queue = nullptr;
    }
    s_ready = false;
    s_worker_busy.store(false, std::memory_order_release);
    s_quiesce.store(false, std::memory_order_release);
}

bool DeepDogFaceFacedbIsReady() {
    return s_ready;
}

void DeepDogFaceFacedbQuiesceBegin() {
    s_quiesce.store(true, std::memory_order_release);
}

void DeepDogFaceFacedbQuiesceEnd() {
    s_quiesce.store(false, std::memory_order_release);
}

bool DeepDogFaceFacedbWaitIdle(int timeout_ms) {
    for (int elapsed = 0; elapsed <= timeout_ms; elapsed += 50) {
        const bool queue_empty = !s_queue || uxQueueMessagesWaiting(s_queue) == 0;
        if (queue_empty && !s_worker_busy.load(std::memory_order_acquire)) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    ESP_LOGW(TAG, "facedb wait idle timeout queue=%u busy=%d",
             s_queue ? (unsigned)uxQueueMessagesWaiting(s_queue) : 0u,
             s_worker_busy.load(std::memory_order_acquire) ? 1 : 0);
    return false;
}

esp_err_t DeepDogFaceFacedbEnrollFeatSync(dl::TensorBase* feat) {
    return RunSyncOp(Op::EnrollFeat, feat, 0);
}

esp_err_t DeepDogFaceFacedbDeleteFeatSync(uint16_t local_id) {
    return RunSyncOp(Op::DeleteFeat, nullptr, local_id);
}

esp_err_t DeepDogFaceFacedbClearAllSync() {
    return RunSyncOp(Op::ClearAll, nullptr, 0);
}

#else

bool DeepDogFaceFacedbInit() {
    return true;
}
void DeepDogFaceFacedbShutdown() {}
bool DeepDogFaceFacedbIsReady() {
    return false;
}
void DeepDogFaceFacedbQuiesceBegin() {}
void DeepDogFaceFacedbQuiesceEnd() {}
bool DeepDogFaceFacedbWaitIdle(int) {
    return true;
}
esp_err_t DeepDogFaceFacedbEnrollFeatSync(dl::TensorBase*) {
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t DeepDogFaceFacedbDeleteFeatSync(uint16_t) {
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t DeepDogFaceFacedbClearAllSync() {
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t DeepDogFaceRecognizeFacedbEnrollFeat(dl::TensorBase*) {
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t DeepDogFaceRecognizeFacedbDeleteFeat(uint16_t) {
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t DeepDogFaceRecognizeFacedbClearAllFeats() {
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
