#include "app_ai_context.hpp"
#include "config.h"
#include "face_explain.hpp"
#include "frame_queue.hpp"
#include "camera.h"

#include <esp_log.h>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#define TAG "app_ai"

// 模拟「本地未识别」：连续收到 20 帧（约 0.5s 间隔 = 10s）后调用 Explain 请求服务器识别。
// 每收到一帧仅打 log，不进行液晶屏显示，降低内存（无 ShowQueuedFrameOnDisplay 的 RGB565 缓冲分配）。
namespace app_ai {
namespace detail {

// 收到 20 帧后触发 Explain 的帧数阈值（0.5s/帧 ≈ 10s）
static constexpr unsigned kExplainFrameThreshold = 20;

void FaceExplainTask(void* pv) {
    auto* ctx = static_cast<AppAIContext*>(pv);
    if (!ctx || !ctx->q_ai || !ctx->camera) {
        vTaskDelete(nullptr);
        return;
    }
    ESP_LOGI(TAG, "FaceExplainTask started (threshold=%u, no display)", kExplainFrameThreshold);
    QueuedFrame qframe;
    unsigned frame_count = 0;
    while (true) {
        if (xQueueReceive(ctx->q_ai, &qframe, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        frame_count++;
        ESP_LOGI(TAG, "FaceExplain: received frame %u", frame_count);

        if (frame_count >= kExplainFrameThreshold) {
            frame_count = 0;
            try {
                std::string response = ctx->camera->Explain(deep_thumble::FACE_EXPLAIN_QUESTION);
                ESP_LOGI(TAG, "FaceExplain: Explain done, response len=%zu", response.size());
            } catch (const std::exception& e) {
                ESP_LOGE(TAG, "FaceExplain: Explain failed: %s", e.what());
            }
        }

        ctx->pool.ReturnBuffer(qframe.data);
    }
}

}  // namespace detail
}  // namespace app_ai
