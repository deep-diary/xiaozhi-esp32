#pragma once

#include <cstddef>
#include <cstdint>

/** 单张人脸（像素坐标，与输入 RGB565 帧同宽高） */
struct DeepDogFaceBox {
    float x0 = 0;
    float y0 = 0;
    float x1 = 0;
    float y1 = 0;
    float score = 0;
};

/** 线程安全快照：供 HTTP JSON 读取 */
struct DeepDogFaceSnapshot {
    uint16_t frame_w = 0;
    uint16_t frame_h = 0;
    uint32_t ts_ms = 0;
    int count = 0;
    bool feature_enabled = false;  // 用户开关（网页）
    DeepDogFaceBox faces[8];
};
