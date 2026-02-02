#pragma once

#include "frame_queue.hpp"

#include <vector>

namespace app_ai {

/**
 * 单个人脸检测结果（与 esp-who WhoDetect rescale 后的坐标一致：原图/显示尺寸）。
 * 下一阶段人脸识别可在此扩展 keypoint / id 等。
 */
struct FaceDetectResult {
    float box[4];  // x0, y0, x1, y1 in frame coordinates
    float score;
};

/**
 * 统一人脸检测核心：与 esp-who 的模型与 rescale 逻辑对齐。
 * 输入 QueuedFrame（format=1 RGB565 或 format=3 YUYV），内部做格式转换、resize 到 320x240、
 * 亮度过滤、HumanFaceDetect::run、rescale_detect_result 风格映射与阈值/最小框过滤。
 *
 * @param qframe 队列帧（会被修改：format=3 时原地转为 RGB565）
 * @param out_results 输出：已映射到帧尺寸且过滤后的检测框（空表示无人脸或跳过）
 * @return 是否执行了检测（false 表示格式不支持、过暗等未跑模型）
 */
bool RunFaceDetectCore(QueuedFrame* qframe, std::vector<FaceDetectResult>* out_results);

}  // namespace app_ai
