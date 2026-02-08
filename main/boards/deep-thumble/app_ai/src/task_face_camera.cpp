#include "app_ai_context.hpp"
#include "config.h"
#include "camera.h"
#include "frame_queue.hpp"

#include <esp_log.h>
#include <cstring>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#define TAG "app_ai"

namespace app_ai {
namespace detail {

void FaceCameraTask(void* pv) {
    auto* ctx = static_cast<AppAIContext*>(pv);
    if (!ctx || !ctx->q_raw || !ctx->camera) {
        vTaskDelete(nullptr);
        return;
    }
    ESP_LOGI(TAG, "FaceCameraTask started");
    CameraFrame frame;
    QueuedFrame qframe;
    while (true) {
        bool cap_ok = ctx->camera->CaptureOnly();
        if (!cap_ok) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (!ctx->camera->GetLastFrame(&frame) || !frame.data || frame.width == 0 || frame.height == 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        uint8_t* buf = ctx->pool.TakeBuffer();
        if (!buf) {
            ESP_LOGW(TAG, "FaceCamera: no pool buffer, drop frame");
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        const uint16_t out_w = FACE_QUEUE_FRAME_WIDTH;
        const uint16_t out_h = FACE_QUEUE_FRAME_HEIGHT;
        const size_t out_len = FACE_QUEUE_FRAME_MAX_BYTES;
        // 相机 240×240，直接拷贝入队（无下采样）
        size_t copy_len = frame.len;
        if (copy_len > out_len) {
            copy_len = out_len;
        }
        memcpy(buf, frame.data, copy_len);
        qframe.data = buf;
        qframe.len = copy_len;
        qframe.width = out_w;
        qframe.height = out_h;
        qframe.format = frame.format;
        if (xQueueSend(ctx->q_raw, &qframe, pdMS_TO_TICKS(100)) != pdTRUE) {
            ctx->pool.ReturnBuffer(buf);
            ESP_LOGW(TAG, "FaceCamera: raw queue full, return buffer");
        }
        vTaskDelay(pdMS_TO_TICKS(FACE_CAMERA_CAPTURE_INTERVAL_MS));
    }
}

}  // namespace detail
}  // namespace app_ai
