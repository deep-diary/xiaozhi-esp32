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
 * 统一人脸检测核心：与 esp-who who_detect 对齐，构造 dl::image::img_t 后直接 run(img)。
 * 输入 QueuedFrame（format=1 RGB565 或 format=3 YUYV）；format=3 时先转 RGB565，再按需做 RGB565 字节对调后送入 run(img)。
 *
 * @param qframe 队列帧（format=3 时会被原地转为 RGB565）
 * @param out_results 输出：已映射到帧尺寸的检测框（空表示无人脸或跳过）
 * @return 是否执行了检测（false 表示格式不支持等未跑模型）
 */
bool RunFaceDetectCore(QueuedFrame* qframe, std::vector<FaceDetectResult>* out_results);

/**
 * 提前创建人脸检测器并跑 240×240 全黑自检（在启动 FaceCamera/FaceAI 任务之前调用）。
 * 若此处得到 0/0 而原先「首次 RunFaceDetectCore 时创建」得到多框，可认定是创建时机/堆状态导致；
 * 此后 RunFaceDetectCore 会复用该 detector，不再在任务内创建。
 */
void CreateFaceDetectorEarly();

}  // namespace app_ai
