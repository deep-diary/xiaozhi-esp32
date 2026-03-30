/**
 * Deep-dog 人脸检测：复用 Espressif human_face_detect（与 deep-thumble app_ai 同源思路）。
 */
#include "sdkconfig.h"

#include "face_ai_config.h"
#include "face_ai_types.h"

#include <esp_heap_caps.h>
#include <esp_log.h>

#include <cstring>
#include <list>
#include <vector>

#if DEEP_DOG_FACE_AI_ENABLE && defined(CONFIG_IDF_TARGET_ESP32S3)

#include "human_face_detect.hpp"
#include "dl_image_define.hpp"
#include "dl_tensor_base.hpp"

#define TAG "dog_face_det"

#ifndef DEEP_DOG_FACE_DETECT_BOX_SWAP_XY
#define DEEP_DOG_FACE_DETECT_BOX_SWAP_XY 0
#endif

#ifndef DEEP_DOG_FACE_DETECT_MIN_BOX_PX
#define DEEP_DOG_FACE_DETECT_MIN_BOX_PX 0
#endif

static HumanFaceDetect* s_detector = nullptr;

bool DeepDogFaceDetectRun(const uint8_t* rgb565, size_t len, uint16_t w, uint16_t h, std::vector<DeepDogFaceBox>* out);

#if DEEP_DOG_FACE_DETECT_SKIP_UNIFORM_DARK
/**
 * 挡镜头/暗箱：像素常为非零噪声但整幅仍「很暗、对比度低」，与 calloc 全黑自检不同。
 * 若采样全为 0 返回 false，让上层照常跑模型（自检仍验证链路）。
 */
static bool FrameIsUniformDarkNoFaceRgb565(const uint8_t* buf, uint16_t w, uint16_t h) {
    const int step = DEEP_DOG_FACE_DETECT_UD_SAMPLE_STEP;
    if (step <= 0 || w < 8 || h < 8 || buf == nullptr) {
        return false;
    }
    uint32_t sum = 0;
    uint32_t n = 0;
    int min_g = 63;
    int max_g = 0;
    for (uint32_t y = 0; y < (uint32_t)h; y += (uint32_t)step) {
        const uint8_t* row = buf + (size_t)y * (size_t)w * 2u;
        for (uint32_t x = 0; x < (uint32_t)w; x += (uint32_t)step) {
            const uint16_t pix = static_cast<uint16_t>(row[x * 2u]) | (static_cast<uint16_t>(row[x * 2u + 1u]) << 8);
            const int g = (static_cast<int>(pix) >> 5) & 0x3F;
            sum += static_cast<uint32_t>(g);
            n++;
            if (g < min_g) {
                min_g = g;
            }
            if (g > max_g) {
                max_g = g;
            }
        }
    }
    if (n == 0) {
        return false;
    }
    const int mean = static_cast<int>(sum / n);
    const int range = max_g - min_g;
    if (min_g == 0 && max_g == 0 && mean == 0) {
        return false;
    }
    return mean <= DEEP_DOG_FACE_DETECT_UD_MAX_MEAN_G && range <= DEEP_DOG_FACE_DETECT_UD_MAX_RANGE_G;
}
#endif

/** 摄像头紧密 RGB565 小端 → 8bit RGB，供 DL_IMAGE_PIX_TYPE_RGB888（与旧 who 例「3 通道」一致） */
static void Rgb565PackedLeToRgb888(const uint8_t* src565, uint8_t* dst888, uint16_t w, uint16_t h) {
    const size_t n = (size_t)w * (size_t)h;
    for (size_t i = 0; i < n; i++) {
        const uint16_t p =
            static_cast<uint16_t>(src565[i * 2u]) | (static_cast<uint16_t>(src565[i * 2u + 1u]) << 8);
        const int r5 = (static_cast<int>(p) >> 11) & 0x1F;
        const int g6 = (static_cast<int>(p) >> 5) & 0x3F;
        const int b5 = static_cast<int>(p) & 0x1F;
        dst888[i * 3u + 0u] = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
        dst888[i * 3u + 1u] = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
        dst888[i * 3u + 2u] = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
    }
}

static void rescale_boxes(const std::list<dl::detect::result_t>& raw, uint16_t frame_w, uint16_t frame_h,
                          uint16_t detect_input_w, uint16_t detect_input_h, std::vector<DeepDogFaceBox>* out) {
    out->clear();
    const float sx = detect_input_w > 0 ? static_cast<float>(frame_w) / detect_input_w : 1.f;
    const float sy = detect_input_h > 0 ? static_cast<float>(frame_h) / detect_input_h : 1.f;

#if DEEP_DOG_FACE_DETECT_BOX_SWAP_XY
#define BOX_X0(r) ((r).box[1])
#define BOX_Y0(r) ((r).box[0])
#define BOX_X1(r) ((r).box[3])
#define BOX_Y1(r) ((r).box[2])
#else
#define BOX_X0(r) ((r).box[0])
#define BOX_Y0(r) ((r).box[1])
#define BOX_X1(r) ((r).box[2])
#define BOX_Y1(r) ((r).box[3])
#endif

    for (const auto& r : raw) {
        float x0 = BOX_X0(r) * sx;
        float y0 = BOX_Y0(r) * sy;
        float x1 = BOX_X1(r) * sx;
        float y1 = BOX_Y1(r) * sy;
        if (x0 < 0.f) {
            x0 = 0.f;
        }
        if (y0 < 0.f) {
            y0 = 0.f;
        }
        if (x1 > static_cast<float>(frame_w)) {
            x1 = static_cast<float>(frame_w);
        }
        if (y1 > static_cast<float>(frame_h)) {
            y1 = static_cast<float>(frame_h);
        }
#if DEEP_DOG_FACE_DETECT_MIN_BOX_PX > 0
        if ((x1 - x0) < static_cast<float>(DEEP_DOG_FACE_DETECT_MIN_BOX_PX) ||
            (y1 - y0) < static_cast<float>(DEEP_DOG_FACE_DETECT_MIN_BOX_PX)) {
            continue;
        }
#endif
        DeepDogFaceBox b;
        b.x0 = x0;
        b.y0 = y0;
        b.x1 = x1;
        b.y1 = y1;
        b.score = r.score;
        out->push_back(b);
    }
#undef BOX_X0
#undef BOX_Y0
#undef BOX_X1
#undef BOX_Y1
}

bool DeepDogFaceDetectInit() {
    if (s_detector != nullptr) {
        return true;
    }
    s_detector = new HumanFaceDetect(static_cast<HumanFaceDetect::model_type_t>(CONFIG_DEFAULT_HUMAN_FACE_DETECT_MODEL),
                                     false);
    if (s_detector == nullptr) {
        ESP_LOGE(TAG, "HumanFaceDetect alloc failed");
        return false;
    }
    /** 组件默认 MSR/MNP score=0.5，在部分集成下量化输出易大量过阈；与 thumble 文档中「全黑仍多框」同属后验过滤问题。 */
    s_detector->set_score_thr(DEEP_DOG_FACE_DETECT_MSR_SCORE_THR, 0);
    s_detector->set_score_thr(DEEP_DOG_FACE_DETECT_MNP_SCORE_THR, 1);
    s_detector->set_nms_thr(DEEP_DOG_FACE_DETECT_MSR_NMS_THR, 0);
    s_detector->set_nms_thr(DEEP_DOG_FACE_DETECT_MNP_NMS_THR, 1);
    ESP_LOGI(TAG,
             "HumanFaceDetect ready (model=%d msr>=%.2f mnp>=%.2f min_box=%d rgb565_swap=%d input_rgb888=%d)",
             (int)CONFIG_DEFAULT_HUMAN_FACE_DETECT_MODEL, (double)DEEP_DOG_FACE_DETECT_MSR_SCORE_THR,
             (double)DEEP_DOG_FACE_DETECT_MNP_SCORE_THR, DEEP_DOG_FACE_DETECT_MIN_BOX_PX,
             DEEP_DOG_FACE_DETECT_RGB565_SWAP, DEEP_DOG_FACE_DETECT_INPUT_RGB888);

#if DEEP_DOG_FACE_DETECT_BLACK_SELFTEST_LOG
    {
        const uint16_t tw = 240, th = 240;
        const size_t tb = (size_t)tw * (size_t)th * 2u;
        uint8_t* black = (uint8_t*)heap_caps_calloc(1, tb, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!black) {
            black = (uint8_t*)heap_caps_calloc(1, tb, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (black) {
            std::vector<DeepDogFaceBox> self_out;
            (void)DeepDogFaceDetectRun(black, tb, tw, th, &self_out);
            if (!self_out.empty()) {
                ESP_LOGW(TAG,
                         "black 240x240 self-test: still %zu box(es) after thr/NMS/min_box — 与 thumble "
                         "face-detection-root-cause.md「全黑仍多框」一致，非分区错绑；可再调高 *_SCORE_THR 或对照 esp-who 同板示例",
                         self_out.size());
            } else {
                ESP_LOGI(TAG, "black 240x240 self-test: 0 boxes (threshold pipeline ok for black)");
            }
            heap_caps_free(black);
        }
    }
#endif
    return true;
}

void DeepDogFaceDetectDeinit() {
    delete s_detector;
    s_detector = nullptr;
}

bool DeepDogFaceDetectRun(const uint8_t* rgb565, size_t len, uint16_t w, uint16_t h, std::vector<DeepDogFaceBox>* out) {
    if (!out || !rgb565 || w == 0 || h == 0) {
        return false;
    }
    const size_t need = (size_t)w * (size_t)h * 2u;
    if (len < need) {
        return false;
    }
    if (!DeepDogFaceDetectInit()) {
        return false;
    }

#if DEEP_DOG_FACE_DETECT_SKIP_UNIFORM_DARK
    if (FrameIsUniformDarkNoFaceRgb565(rgb565, w, h)) {
        out->clear();
        return true;
    }
#endif

#if DEEP_DOG_FACE_DETECT_INPUT_RGB888
    const size_t rgb_len = (size_t)w * (size_t)h * 3u;
    uint8_t* rgb888 = (uint8_t*)heap_caps_malloc(rgb_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (rgb888 == nullptr) {
        rgb888 = (uint8_t*)heap_caps_malloc(rgb_len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (rgb888 == nullptr) {
        out->clear();
        return false;
    }
    Rgb565PackedLeToRgb888(rgb565, rgb888, w, h);
    dl::image::img_t img = {.data = rgb888, .width = w, .height = h, .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB888};
    const auto& raw = s_detector->run(img);
    rescale_boxes(raw, w, h, img.width, img.height, out);
    heap_caps_free(rgb888);
#else
    const void* detect_src = rgb565;
    uint8_t* swap_buf = nullptr;
#if DEEP_DOG_FACE_DETECT_RGB565_SWAP
    swap_buf = (uint8_t*)heap_caps_malloc(need, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!swap_buf) {
        swap_buf = (uint8_t*)heap_caps_malloc(need, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (swap_buf) {
        memcpy(swap_buf, rgb565, need);
        auto* p = reinterpret_cast<uint16_t*>(swap_buf);
        for (size_t i = 0; i < (size_t)w * h; i++) {
            p[i] = static_cast<uint16_t>((p[i] >> 8) | (p[i] << 8));
        }
        detect_src = swap_buf;
    }
#endif

    dl::image::img_t img = {.data = const_cast<void*>(detect_src),
                            .width = w,
                            .height = h,
                            .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565};
    const auto& raw = s_detector->run(img);
    rescale_boxes(raw, w, h, img.width, img.height, out);

    if (swap_buf) {
        heap_caps_free(swap_buf);
    }
#endif
    return true;
}

#else  // !ENABLE || !S3

bool DeepDogFaceDetectInit() {
    return false;
}
void DeepDogFaceDetectDeinit() {}
bool DeepDogFaceDetectRun(const uint8_t*, size_t, uint16_t, uint16_t, std::vector<DeepDogFaceBox>*) {
    return false;
}

#endif
