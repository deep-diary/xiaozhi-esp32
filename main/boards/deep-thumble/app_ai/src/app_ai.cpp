#include "app_ai.hpp"
#include "app_ai_context.hpp"
#include "config.h"
#include "camera.h"
#include "display/display.h"
#include "application.h"
#include "device_state.h"
#include "frame_queue.hpp"
#include "face_recognition.hpp"

#include <esp_log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#define TAG "app_ai"

namespace app_ai {

void Start(Camera* camera, Display* display) {
    if (!camera || !display) {
        ESP_LOGW(TAG, "Start skipped: camera or display null");
        return;
    }

    detail::AppAIContext* ctx = new detail::AppAIContext();
    ctx->camera = camera;
    ctx->display = display;

    if (!ctx->pool.Init(FACE_QUEUE_FRAME_POOL_SIZE, FACE_QUEUE_FRAME_MAX_BYTES)) {
        ESP_LOGE(TAG, "pool init failed");
        delete ctx;
        return;
    }
    ctx->q_raw = xQueueCreate(FACE_QUEUE_RAW_DEPTH, sizeof(QueuedFrame));
    ctx->q_ai = xQueueCreate(FACE_QUEUE_AI_DEPTH, sizeof(QueuedFrame));
    if (!ctx->q_raw || !ctx->q_ai) {
        ESP_LOGE(TAG, "queues create failed");
        ctx->pool.Deinit();
        delete ctx;
        return;
    }

    ctx->face_recognition = new FaceRecognition(camera, display);
    ctx->face_recognition->SetExplainOnlyWhenAwake([]() {
        DeviceState s = Application::GetInstance().GetDeviceState();
        return s == kDeviceStateConnecting || s == kDeviceStateListening || s == kDeviceStateSpeaking;
    });

    // 人脸管道单独做 320×240 预览，关闭 Capture() 内 640×480 预览，避免双重预览抢 PSRAM
    ctx->camera->SetCapturePreviewEnabled(false);

    ESP_LOGI(TAG, "dual-queue: pool=%d raw=%d ai=%d",
             FACE_QUEUE_FRAME_POOL_SIZE, FACE_QUEUE_RAW_DEPTH, FACE_QUEUE_AI_DEPTH);

    BaseType_t ret = xTaskCreate(detail::FaceCameraTask, "face_camera", FACE_CAMERA_TASK_STACK, ctx,
                                 FACE_CAMERA_TASK_PRIORITY, nullptr);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "FaceCamera task create failed");
    }
    ret = xTaskCreate(detail::FaceAITask, "face_ai", FACE_AI_TASK_STACK, ctx, FACE_AI_TASK_PRIORITY, nullptr);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "FaceAI task create failed");
    }
    ret = xTaskCreate(detail::FaceExplainTask, "face_explain", FACE_DISPLAY_TASK_STACK, ctx,
                      FACE_DISPLAY_TASK_PRIORITY, nullptr);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "FaceExplain task create failed");
    }
}

}  // namespace app_ai
