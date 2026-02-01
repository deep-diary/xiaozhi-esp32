#include "app_ai_context.hpp"
#include "config.h"
#include "frame_queue.hpp"
#include "face_recognition.hpp"

#include <esp_log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#define TAG "app_ai"

namespace app_ai {
namespace detail {

// q_ai 保持相机原格式（如 YUYV），与 MCP Capture→Explain 一致；Explain 内部 image_to_jpeg 会按格式编码，显示侧再按需转 RGB565
void FaceAITask(void* pv) {
    auto* ctx = static_cast<AppAIContext*>(pv);
    if (!ctx || !ctx->q_raw || !ctx->q_ai || !ctx->face_recognition) {
        vTaskDelete(nullptr);
        return;
    }
    ESP_LOGI(TAG, "FaceAITask started (passthrough=%d)", (int)FACE_AI_PASSTHROUGH);
    QueuedFrame qframe;
    while (true) {
        if (xQueueReceive(ctx->q_raw, &qframe, portMAX_DELAY) != pdTRUE) {
            continue;
        }
#if !FACE_AI_PASSTHROUGH
        ctx->face_recognition->ProcessOneFrame(&qframe);
#endif
        if (xQueueSend(ctx->q_ai, &qframe, pdMS_TO_TICKS(200)) != pdTRUE) {
            ctx->pool.ReturnBuffer(qframe.data);
            ESP_LOGW(TAG, "FaceAI: AI queue full, return buffer");
        }
    }
}

}  // namespace detail
}  // namespace app_ai
