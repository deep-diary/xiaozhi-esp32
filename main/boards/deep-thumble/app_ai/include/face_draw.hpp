#pragma once

// 在 RGB565 上绘制人脸框与 ID/姓名。当使用 MSR01+MNP01（dl::detect::result_t）时，
// 可改用 main/boards/deep-thumble/ai/who_ai_utils.hpp 的 draw_detection_result 及 fb_gfx 文本。

#include <cstdint>
#include <vector>
#include "face_recognition.hpp"

namespace deep_thumble {

void DrawFaceBoxesOnRgb565(uint8_t* rgb565_buf, uint16_t buf_w, uint16_t buf_h,
                           const std::vector<FaceBox>& boxes);

}  // namespace deep_thumble
