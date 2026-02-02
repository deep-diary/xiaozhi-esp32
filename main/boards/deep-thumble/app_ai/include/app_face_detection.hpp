#pragma once

#include "frame_queue.hpp"

namespace app_ai {

/**
 * @brief 对一帧图像做人脸检测并打印结果（是否有人脸、人脸框位置与大小）。
 *        使用与 face_recognition 相同的 ESP-DL HumanFaceDetect，不依赖 esp_camera。
 *
 * 说明：HumanFaceDetect（human_face_detect 组件）内部即为 MSR_S8_V1 + MNP_S8_V1
 * 两阶段模型，与 esp-who 里的 MSR01+MNP01 是同一套方案，仅 API 封装不同（run(img) vs 分步 infer）。
 * 若效果不佳，可优先调 config.h：FACE_DETECT_SCORE_THRESHOLD、FACE_DETECT_MIN_BOX_SIZE、
 * FACE_DETECT_MIN_LUMINANCE、FACE_DETECT_BOX_SWAP_XY；详见 docs/face-detection-log-analysis-deep-thumble.md。
 *
 * @param qframe 队列帧（format=1 为 RGB565，format=3 会先转 RGB565 再检测）
 * @return 检测到的人脸数量（0 表示无人脸或格式不支持/过暗等）
 */
int RunFaceDetectionAndLog(QueuedFrame* qframe);

}  // namespace app_ai
