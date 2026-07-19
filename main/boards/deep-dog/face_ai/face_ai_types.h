#pragma once

#include <cstddef>
#include <cstdint>

/** 识别来源（S04） */
enum class DeepDogFaceRecognizeSource : uint8_t {
    None = 0,
    Session = 1,
    Nvs = 2,      // 持久化库命中（facedb + NVS meta）
    Enrolled = 3,
};

/** 单张人脸（像素坐标，与输入 RGB565 帧同宽高） */
struct DeepDogFaceBox {
    float x0 = 0;
    float y0 = 0;
    float x1 = 0;
    float y1 = 0;
    float score = 0;
    /** 5 点关键点 [x1,y1,...,x5,y5]，与 dl::detect::result_t::keypoint 一致 */
    int kp[10] = {};
    uint8_t kp_n = 0;
    int local_id = 0;
    char display_name[16] = {};
    DeepDogFaceRecognizeSource recognize_source = DeepDogFaceRecognizeSource::None;
};

/** 线程安全快照：供 HTTP JSON 读取 */
struct DeepDogFaceSnapshot {
    uint16_t frame_w = 0;
    uint16_t frame_h = 0;
    uint32_t ts_ms = 0;
    int count = 0;
    bool feature_enabled = false;  // 用户开关（网页）
    DeepDogFaceBox faces[8];
    /** 主脸（最高分）识别结果，便于顶层 JSON */
    int primary_local_id = 0;
    char primary_display_name[16] = {};
    DeepDogFaceRecognizeSource primary_source = DeepDogFaceRecognizeSource::None;
};

inline const char* DeepDogFaceRecognizeSourceStr(DeepDogFaceRecognizeSource s) {
    switch (s) {
        case DeepDogFaceRecognizeSource::Session:
            return "session";
        case DeepDogFaceRecognizeSource::Nvs:
            return "nvs";
        case DeepDogFaceRecognizeSource::Enrolled:
            return "enrolled";
        default:
            return "none";
    }
}
