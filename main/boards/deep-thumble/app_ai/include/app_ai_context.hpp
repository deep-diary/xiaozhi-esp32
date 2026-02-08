#pragma once

#include "frame_queue.hpp"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class Camera;
class Display;
class FaceRecognition;

namespace app_ai {
namespace detail {

struct AppAIContext {
    Camera* camera = nullptr;
    Display* display = nullptr;
    FaceFramePool pool;
    QueueHandle_t q_raw = nullptr;
    QueueHandle_t q_ai = nullptr;
    FaceRecognition* face_recognition = nullptr;
};

void FaceCameraTask(void* pv);
void FaceAITask(void* pv);
void FaceExplainTask(void* pv);

/// 将 q_ai 中的一帧显示到屏幕（供 TickDisplay 使用）
void ShowQueuedFrameOnDisplay(AppAIContext* ctx, QueuedFrame* qframe);

}  // namespace detail
}  // namespace app_ai
