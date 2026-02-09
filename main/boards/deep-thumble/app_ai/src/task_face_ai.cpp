#include "app_ai_context.hpp"
#include "config.h"
#include "frame_queue.hpp"
#include "face_recognition.hpp"

#include <esp_log.h>
#include <esp_timer.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#define TAG "app_ai"
#define FACE_AI_FPS_LOG_INTERVAL_MS 1000

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
    int64_t last_fps_log_us = esp_timer_get_time();
    uint32_t frame_count = 0;
    while (true) {
        if (xQueueReceive(ctx->q_raw, &qframe, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        frame_count++;
#if FACE_AI_PASSTHROUGH
        // 透传模式：不跑检测，直接转发 q_raw→q_ai，避免长时间占用 buffer 导致 pool 耗尽
        (void)0;
#else
        // 非透传：只跑一次检测+画框（ProcessOneFrame 内）；FACE_DETECTION_ONLY=1 时仅画框不跑识别
        ctx->face_recognition->ProcessOneFrame(&qframe);
        taskYIELD();  // 单帧处理耗时较长，让出 CPU 给 IDLE/其他任务，避免 task_wdt
#endif
        if (xQueueSend(ctx->q_ai, &qframe, pdMS_TO_TICKS(200)) != pdTRUE) {
            ctx->pool.ReturnBuffer(qframe.data);
            ESP_LOGW(TAG, "FaceAI: AI queue full, return buffer");
        }
        // 每隔 1s 打印当前帧率，便于对比使能 AI 前后的帧率变化
        int64_t now_us = esp_timer_get_time();
        if ((now_us - last_fps_log_us) >= (FACE_AI_FPS_LOG_INTERVAL_MS * 1000)) {
            float elapsed_s = (float)(now_us - last_fps_log_us) / 1e6f;
            float fps = elapsed_s > 0 ? (float)frame_count / elapsed_s : 0;
            ESP_LOGI(TAG, "FaceAI fps=%.1f (passthrough=%d)", fps, (int)FACE_AI_PASSTHROUGH);
            frame_count = 0;
            last_fps_log_us = now_us;
        }
    }
}

}  // namespace detail
}  // namespace app_ai
