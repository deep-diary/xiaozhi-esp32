#pragma once

class Camera;
class Display;

namespace app_ai {

/// 启动双队列人脸管线：相机任务 → AI 任务；显示由主循环调用 TickDisplay() 从 q_ai 取帧。
/// 由板级在相机、显示就绪后调用；camera 建议为已与 MCP 串行化的包装（如 CameraCaptureLockWrapper）。
void Start(Camera* camera, Display* display);

/// 主循环每轮调用：从 q_ai 非阻塞取一帧并显示到屏幕。
void TickDisplay();

}  // namespace app_ai
