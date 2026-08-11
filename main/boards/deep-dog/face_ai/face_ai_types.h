#pragma once

#include "face_ai_config.h"

#include <cstddef>
#include <cstdint>

/** 识别来源（S04） */
enum class DeepDogFaceRecognizeSource : uint8_t {
    None = 0,
    Session = 1,
    Nvs = 2,      // 持久化库命中（facedb + NVS meta）
    Enrolled = 3,
};

/** 人脸管线模式：live=检测可高频、识别低频；identity=检测与识别同间隔 */
enum class DeepDogFacePipeline : uint8_t {
    Live = 0,
    Identity = 1,
};

inline const char* DeepDogFacePipelineStr(DeepDogFacePipeline p) {
    switch (p) {
        case DeepDogFacePipeline::Identity:
            return "identity";
        case DeepDogFacePipeline::Live:
        default:
            return "live";
    }
}

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
    char display_name[32] = {};
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
    char primary_display_name[32] = {};
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

/** 已注册人脸条目（canonical 或 meta 索引；供 list/registry） */
struct DeepDogFaceEnrolledEntry {
    int local_id = 0;
    int canonical_id = 0;
    char display_name[32] = {};
    char immich_person_id[40] = {};
    char immich_asset_id[48] = {};
    uint32_t updated_at = 0;
    /** Unix 秒；最后一次识别命中该槽（LRU / UI「上次见面」） */
    uint32_t last_seen_at = 0;
    bool name_pending = false;
    int aliases[DEEP_DOG_FACE_REGISTRY_MAX_ALIASES] = {};
    int alias_count = 0;
};
