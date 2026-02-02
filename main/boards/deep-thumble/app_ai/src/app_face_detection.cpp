/**
 * 人脸检测并打 log（是否有人脸、框大小等）。
 * 使用统一检测核心 face_detect_core（与 esp-who WhoDetect 模型与 rescale 对齐）。
 * 效果不佳时优先调 config.h：FACE_DETECT_SCORE_THRESHOLD、FACE_DETECT_MIN_BOX_SIZE、FACE_DETECT_MIN_LUMINANCE。
 */
#include "app_face_detection.hpp"
#include "face_detect_core.hpp"
#include "config.h"
#include "frame_queue.hpp"

#include <esp_log.h>

#define TAG "FaceDetect"

namespace app_ai {

int RunFaceDetectionAndLog(QueuedFrame* qframe) {
    if (!qframe || !qframe->data || qframe->width == 0 || qframe->height == 0) {
        return 0;
    }

    std::vector<FaceDetectResult> results;
    if (!RunFaceDetectCore(qframe, &results)) {
        return 0;
    }

    const int face_count = static_cast<int>(results.size());

    static unsigned int s_consecutive_no_face = 0;
    static unsigned int s_consecutive_one_face = 0;

    for (size_t i = 0; i < results.size(); i++) {
        const auto& r = results[i];
        int x0 = static_cast<int>(r.box[0]);
        int y0 = static_cast<int>(r.box[1]);
        int box_w = static_cast<int>(r.box[2] - r.box[0]);
        int box_h = static_cast<int>(r.box[3] - r.box[1]);
        ESP_LOGI(TAG, "Face detected [%zu]: box=(%d,%d) size=%dx%d score=%.2f",
                 i, x0, y0, box_w, box_h, r.score);
    }

    if (face_count == 0) {
        ESP_LOGI(TAG, "Face not detected (no valid box after filter).");
        s_consecutive_no_face++;
        s_consecutive_one_face = 0;
        if (s_consecutive_no_face == 8) {
            ESP_LOGI(TAG, "likely_ceiling (8 consecutive no face)");
        }
    } else if (face_count == 1) {
        ESP_LOGI(TAG, "Face detected: %d face(s) total.", face_count);
        s_consecutive_one_face++;
        s_consecutive_no_face = 0;
        if (s_consecutive_one_face == 5) {
            ESP_LOGI(TAG, "likely_face (5 consecutive 1 face)");
        }
    } else {
        ESP_LOGI(TAG, "Face detected: %d face(s) total.", face_count);
        s_consecutive_no_face = 0;
        s_consecutive_one_face = 0;
    }

    return face_count;
}

}  // namespace app_ai
