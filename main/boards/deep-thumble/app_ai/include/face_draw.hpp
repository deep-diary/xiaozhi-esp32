#pragma once

// 在 RGB565 上绘制人脸框与 ID/姓名（本地画框实现，不依赖 who_ai_utils）。

#include <cstdint>
#include <vector>
#include "face_recognition.hpp"

namespace deep_thumble {

void DrawFaceBoxesOnRgb565(uint8_t* rgb565_buf, uint16_t buf_w, uint16_t buf_h,
                           const std::vector<FaceBox>& boxes);

}  // namespace deep_thumble
