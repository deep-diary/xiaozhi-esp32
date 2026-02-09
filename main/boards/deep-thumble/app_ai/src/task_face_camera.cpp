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
    ESP_LOGI(TAG, "FaceCameraTask started (capture first, then take buffer when free)");
    CameraFrame frame;
    QueuedFrame qframe;
    while (true) {
        // 先采集（不占 buffer），再取 buffer 入队；拿不到 buffer 则丢本帧，避免持 buffer 期间阻塞在 CaptureOnly 导致管道卡死
        bool cap_ok = ctx->camera->CaptureOnly();
        if (!cap_ok) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        if (!ctx->camera->GetLastFrame(&frame) || !frame.data || frame.width == 0 || frame.height == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        uint8_t* buf = ctx->pool.TakeBuffer();
        if (!buf) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        const uint16_t out_w = FACE_QUEUE_FRAME_WIDTH;
        const uint16_t out_h = FACE_QUEUE_FRAME_HEIGHT;
        const size_t out_len = FACE_QUEUE_FRAME_MAX_BYTES;
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
        // 无固定延时，有 buffer 就尽快采下一帧
    }
}

}  // namespace detail
}  // namespace app_ai
