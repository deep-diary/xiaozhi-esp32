#pragma once

#include "frame_queue.hpp"

#include <vector>

namespace app_ai {

/**
 * 单个人脸检测结果（坐标已 rescale 到原图/显示尺寸）。
 * 下一阶段人脸识别可在此扩展 keypoint / id 等。
 */
struct FaceDetectResult {
    float box[4];  // x0, y0, x1, y1 in frame coordinates
    float score;
};

/**
 * 统一人脸检测核心：对齐 human_face_detect README 的「实例化 + run(img)」流程。
 * 输入 QueuedFrame（format=1 RGB565 或 format=3 YUYV），内部：格式转换（YUYV→RGB565）、亮度过滤
 * → HumanFaceDetect::run(img)（组件内部将任意尺寸 resize 到模型输入 120×160）→ 后处理（rescale 到帧尺寸、阈值与最小框过滤）。
 *
 * @param qframe 队列帧（会被修改：format=3 时原地转为 RGB565）
 * @param out_results 输出：已映射到帧尺寸且过滤后的检测框（空表示无人脸或跳过）
 * @return 是否执行了检测（false 表示格式不支持、过暗等未跑模型）
 */
bool RunFaceDetectCore(QueuedFrame* qframe, std::vector<FaceDetectResult>* out_results);

}  // namespace app_ai
