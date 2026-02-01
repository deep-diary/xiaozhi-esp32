#pragma once

class Camera;
class Display;

namespace app_ai {

/// 启动双队列人脸管线：相机任务 → AI 任务 → 显示任务。
/// 由板级在相机、显示就绪后调用；camera 建议为已与 MCP 串行化的包装（如 CameraCaptureLockWrapper）。
void Start(Camera* camera, Display* display);

}  // namespace app_ai
