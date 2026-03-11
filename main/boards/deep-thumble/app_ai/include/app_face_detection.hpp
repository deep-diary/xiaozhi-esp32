#pragma once

#include "frame_queue.hpp"

namespace app_ai {

/**
 * @brief 对一帧图像做人脸检测并打印结果（是否有人脸、人脸框位置与大小）。
 *        使用 human_face_detect 组件的 HumanFaceDetect（MSR_S8_V1 + MNP_S8_V1），不依赖 esp_camera。
 *
 * 检测结果现完全采用 human_face_detect 组件输出（无额外阈值/最小框）；可选 config.h：FACE_DETECT_BOX_SWAP_XY 等；详见 docs/face-detection-log-analysis-deep-thumble.md。
 *
 * @param qframe 队列帧（format=1 为 RGB565，format=3 会先转 RGB565 再检测）
 * @return 检测到的人脸数量（0 表示无人脸或格式不支持/过暗等）
 */
int RunFaceDetectionAndLog(QueuedFrame* qframe);

}  // namespace app_ai
