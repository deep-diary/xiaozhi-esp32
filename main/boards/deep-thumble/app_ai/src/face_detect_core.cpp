/**
 * 统一人脸检测核心：基于 human_face_detect 组件（MSR_S8_V1 + MNP_S8_V1）。
 * 输入 QueuedFrame（RGB565），构造 dl::image::img_t 后直接 run(img)，与 esp-who who_detect 对齐。
 */
#include "face_detect_core.hpp"
#include "config.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <cstdio>
#include <cstring>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#if CONFIG_IDF_TARGET_ESP32S3
#include "human_face_detect.hpp"
#include "dl_image_define.hpp"
#include "dl_model_base.hpp"
#include "dl_tensor_base.hpp"
#include "esp_imgfx_color_convert.h"
#endif

#define TAG "FaceDetectCore"

#if defined(CONFIG_HUMAN_FACE_DETECT_MODEL_IN_FLASH_RODATA) && defined(CONFIG_IDF_TARGET_ESP32S3)
// 嵌入模型由 human_face_detect 组件生成，为 C 符号，必须 extern "C" 否则链接报 undefined reference
extern "C" {
extern const uint8_t _binary_human_face_detect_espdl_start[];
extern const uint8_t _binary_human_face_detect_espdl_end[];
}
#endif

namespace app_ai {

#if CONFIG_IDF_TARGET_ESP32S3
// 组件内部 MSR 模型输入为 120×160×3，ImagePreprocessor 会自行 resize；run(img) 结果坐标为传入 img 的宽高
static void rescale_and_filter(
    const std::list<dl::detect::result_t>& raw,
    uint16_t frame_w,
    uint16_t frame_h,
    uint16_t detect_input_w,
    uint16_t detect_input_h,
    std::vector<FaceDetectResult>* out_results)
{
    const float inv_rescale_x = detect_input_w > 0 ? static_cast<float>(frame_w) / detect_input_w : 1.0f;
    const float inv_rescale_y = detect_input_h > 0 ? static_cast<float>(frame_h) / detect_input_h : 1.0f;

#if FACE_DETECT_BOX_SWAP_XY
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
        float x0 = BOX_X0(r) * inv_rescale_x;
        float y0 = BOX_Y0(r) * inv_rescale_y;
        float x1 = BOX_X1(r) * inv_rescale_x;
        float y1 = BOX_Y1(r) * inv_rescale_y;
        if (x0 < 0.f) x0 = 0.f;
        if (y0 < 0.f) y0 = 0.f;
        if (x1 > static_cast<float>(frame_w)) x1 = static_cast<float>(frame_w);
        if (y1 > static_cast<float>(frame_h)) y1 = static_cast<float>(frame_h);
#if FACE_DETECT_MIN_BOX_SIZE > 0
        float bw = x1 - x0;
        float bh = y1 - y0;
        if (bw < FACE_DETECT_MIN_BOX_SIZE || bh < FACE_DETECT_MIN_BOX_SIZE) {
            continue;  // 过滤过小框（杂点/误检）
        }
#endif
        FaceDetectResult fr;
        fr.box[0] = x0;
        fr.box[1] = y0;
        fr.box[2] = x1;
        fr.box[3] = y1;
        fr.score = r.score;
        out_results->push_back(fr);
    }

#undef BOX_X0
#undef BOX_Y0
#undef BOX_X1
#undef BOX_Y1
}

// 文件内共享：提前创建或首次 RunFaceDetectCore 时创建，后续复用
static HumanFaceDetect* s_detector = nullptr;

void CreateFaceDetectorEarly() {
    if (s_detector != nullptr) {
        return;
    }
    s_detector = new HumanFaceDetect(
        static_cast<HumanFaceDetect::model_type_t>(CONFIG_DEFAULT_HUMAN_FACE_DETECT_MODEL), false);
    if (!s_detector) {
        ESP_LOGE(TAG, "CreateFaceDetectorEarly: HumanFaceDetect alloc failed.");
        return;
    }
    // 仅跑 240×240 全黑两次（与 ref/face_detect_black_test 一致），验证「提前创建」是否得到 0/0
    const uint16_t W = 240, H = 240;
    const size_t size_rgb565 = (size_t)W * H * 2;
    uint8_t* black = (uint8_t*)heap_caps_malloc(size_rgb565, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!black) {
        black = (uint8_t*)heap_caps_malloc(size_rgb565, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (black) {
        memset(black, 0, size_rgb565);
        dl::image::img_t img = {
            .data = black,
            .width = W,
            .height = H,
            .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565,
        };
        size_t n1 = s_detector->run(img).size();
        size_t n2 = s_detector->run(img).size();
        heap_caps_free(black);
        ESP_LOGI(TAG, "early black test (earliest in Start, before pool/queues): 1st=%lu 2nd=%lu (expect 0,0; if 0,0 then creation order was the cause)",
                 (unsigned long)n1, (unsigned long)n2);
    } else {
        ESP_LOGW(TAG, "early black test: alloc failed");
    }
}
#else
void CreateFaceDetectorEarly() {}
#endif

bool RunFaceDetectCore(QueuedFrame* qframe, std::vector<FaceDetectResult>* out_results) {
    if (!qframe || !qframe->data || qframe->width == 0 || qframe->height == 0 || !out_results) {
        return false;
    }

    out_results->clear();

    const uint16_t w = qframe->width;
    const uint16_t h = qframe->height;
    const size_t size_rgb565 = (size_t)w * h * 2;

#if CONFIG_IDF_TARGET_ESP32S3
    // 若为 YUYV，先转为 RGB565 再检测（与显示路径一致）
    if (qframe->format == 3) {
        uint8_t* out_buf = (uint8_t*)heap_caps_malloc(size_rgb565, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!out_buf) {
            ESP_LOGW(TAG, "YUYV convert alloc failed.");
            return false;
        }
        esp_imgfx_color_convert_cfg_t cfg = {
            .in_res = {.width = static_cast<int16_t>(w), .height = static_cast<int16_t>(h)},
            .in_pixel_fmt = ESP_IMGFX_PIXEL_FMT_YUYV,
            .out_pixel_fmt = ESP_IMGFX_PIXEL_FMT_RGB565_LE,
            .color_space_std = ESP_IMGFX_COLOR_SPACE_STD_BT601,
        };
        esp_imgfx_color_convert_handle_t handle = nullptr;
        esp_imgfx_err_t err = esp_imgfx_color_convert_open(&cfg, &handle);
        if (err != ESP_IMGFX_ERR_OK || !handle) {
            heap_caps_free(out_buf);
            return false;
        }
        esp_imgfx_data_t in_data = {.data = qframe->data, .data_len = qframe->len};
        esp_imgfx_data_t out_data = {.data = out_buf, .data_len = static_cast<uint32_t>(size_rgb565)};
        err = esp_imgfx_color_convert_process(handle, &in_data, &out_data);
        esp_imgfx_color_convert_close(handle);
        if (err != ESP_IMGFX_ERR_OK) {
            heap_caps_free(out_buf);
            return false;
        }
        memcpy(qframe->data, out_buf, size_rgb565);
        heap_caps_free(out_buf);
        qframe->format = 1;
        qframe->len = size_rgb565;
        taskYIELD();
    }
#endif

    if (qframe->format != 1) {
        ESP_LOGW(TAG, "Frame format not RGB565 (%d), skip detect.", qframe->format);
        return false;
    }

#if CONFIG_IDF_TARGET_ESP32S3
    // 若未在 Start() 里提前 CreateFaceDetectorEarly()，则在此首次创建并做全量自检
    if (s_detector == nullptr) {
#if defined(CONFIG_HUMAN_FACE_DETECT_MODEL_IN_FLASH_RODATA) && !defined(CONFIG_HUMAN_FACE_DETECT_MODEL_IN_SDCARD)
        size_t model_size = _binary_human_face_detect_espdl_end - _binary_human_face_detect_espdl_start;
        ESP_LOGI(TAG, "face model RODATA: %lu bytes (packed msr+mnp, expect ~191KB)", (unsigned long)model_size);
#endif
        s_detector = new HumanFaceDetect(
            static_cast<HumanFaceDetect::model_type_t>(CONFIG_DEFAULT_HUMAN_FACE_DETECT_MODEL), false);
        if (s_detector == nullptr) {
            ESP_LOGE(TAG, "HumanFaceDetect alloc failed.");
            return false;
        }
        ESP_LOGI(TAG, "HumanFaceDetect created (model=%d, lazy_load=0), rgb565_byte_swap=%d",
                 (int)CONFIG_DEFAULT_HUMAN_FACE_DETECT_MODEL, (int)FACE_DETECT_RGB565_BYTE_SWAP);

        // 自检：全黑图应检出 0 张脸。跑两次以区分「首帧未初始化」与「模型/预处理始终异常」
        const uint16_t test_w = FACE_QUEUE_FRAME_WIDTH;
        const uint16_t test_h = FACE_QUEUE_FRAME_HEIGHT;
        const size_t test_bytes = (size_t)test_w * test_h * 2;
        uint8_t* black_buf = (uint8_t*)heap_caps_malloc(test_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!black_buf) {
            black_buf = (uint8_t*)heap_caps_malloc(test_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (black_buf) {
            memset(black_buf, 0, test_bytes);
            dl::image::img_t black_img = {
                .data = black_buf,
                .width = test_w,
                .height = test_h,
                .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565,
            };
            size_t n1 = s_detector->run(black_img).size();
            size_t n2 = s_detector->run(black_img).size();
            heap_caps_free(black_buf);
            if (n1 > 0 || n2 > 0) {
                ESP_LOGW(TAG, "self-test black: 1st=%lu 2nd=%lu face(s) (expect 0,0; see docs/face-detection-root-cause.md)",
                         (unsigned long)n1, (unsigned long)n2);
            } else {
                ESP_LOGI(TAG, "self-test black: 0 face(s) ok");
            }
            // 再用 RGB888 全黑测一次：若仍多张脸则问题在公共路径(resize/模型)，非 RGB565 解析
            const size_t rgb888_bytes = (size_t)test_w * test_h * 3;
            uint8_t* black_rgb888 = (uint8_t*)heap_caps_malloc(rgb888_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!black_rgb888) {
                black_rgb888 = (uint8_t*)heap_caps_malloc(rgb888_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            }
            if (black_rgb888) {
                memset(black_rgb888, 0, rgb888_bytes);
                dl::image::img_t black_img_rgb888 = {
                    .data = black_rgb888,
                    .width = test_w,
                    .height = test_h,
                    .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB888,
                };
                size_t r1 = s_detector->run(black_img_rgb888).size();
                size_t r2 = s_detector->run(black_img_rgb888).size();
                heap_caps_free(black_rgb888);
                ESP_LOGI(TAG, "self-test black RGB888: 1st=%lu 2nd=%lu (if same as RGB565 then bug not in RGB565 path)",
                         (unsigned long)r1, (unsigned long)r2);
            }
            // 改分辨率全黑（320×240，esp-who 常用）：RGB565 + RGB888 各跑两次，排查是否与输入尺寸/缩放有关
            const uint16_t alt_w = 320, alt_h = 240;
            size_t alt_rgb565_bytes = (size_t)alt_w * alt_h * 2;
            uint8_t* alt_565 = (uint8_t*)heap_caps_malloc(alt_rgb565_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!alt_565) {
                alt_565 = (uint8_t*)heap_caps_malloc(alt_rgb565_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            }
            if (alt_565) {
                memset(alt_565, 0, alt_rgb565_bytes);
                dl::image::img_t alt_img_565 = {
                    .data = alt_565,
                    .width = alt_w,
                    .height = alt_h,
                    .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565,
                };
                size_t a1 = s_detector->run(alt_img_565).size();
                size_t a2 = s_detector->run(alt_img_565).size();
                heap_caps_free(alt_565);
                ESP_LOGI(TAG, "self-test black 320x240 RGB565: 1st=%lu 2nd=%lu",
                         (unsigned long)a1, (unsigned long)a2);
            }
            size_t alt_rgb888_bytes = (size_t)alt_w * alt_h * 3;
            uint8_t* alt_888 = (uint8_t*)heap_caps_malloc(alt_rgb888_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!alt_888) {
                alt_888 = (uint8_t*)heap_caps_malloc(alt_rgb888_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            }
            if (alt_888) {
                memset(alt_888, 0, alt_rgb888_bytes);
                dl::image::img_t alt_img_888 = {
                    .data = alt_888,
                    .width = alt_w,
                    .height = alt_h,
                    .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB888,
                };
                size_t b1 = s_detector->run(alt_img_888).size();
                size_t b2 = s_detector->run(alt_img_888).size();
                heap_caps_free(alt_888);
                ESP_LOGI(TAG, "self-test black 320x240 RGB888: 1st=%lu 2nd=%lu",
                         (unsigned long)b1, (unsigned long)b2);
            }
            // 120×160 与 MSR 模型输入一致（无 resize）：若此处 0 而其它尺寸多框，则问题在缩放路径
            const uint16_t native_w = 120, native_h = 160;
            size_t nat_565_bytes = (size_t)native_w * native_h * 2;
            uint8_t* nat_565 = (uint8_t*)heap_caps_malloc(nat_565_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!nat_565) {
                nat_565 = (uint8_t*)heap_caps_malloc(nat_565_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            }
            if (nat_565) {
                memset(nat_565, 0, nat_565_bytes);
                dl::image::img_t nat_img_565 = {
                    .data = nat_565,
                    .width = native_w,
                    .height = native_h,
                    .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565,
                };
                size_t c1 = s_detector->run(nat_img_565).size();
                size_t c2 = s_detector->run(nat_img_565).size();
                heap_caps_free(nat_565);
                ESP_LOGI(TAG, "self-test black 120x160 RGB565: 1st=%lu 2nd=%lu (MSR native size, no resize)",
                         (unsigned long)c1, (unsigned long)c2);
            }
            size_t nat_888_bytes = (size_t)native_w * native_h * 3;
            uint8_t* nat_888 = (uint8_t*)heap_caps_malloc(nat_888_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!nat_888) {
                nat_888 = (uint8_t*)heap_caps_malloc(nat_888_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            }
            if (nat_888) {
                memset(nat_888, 0, nat_888_bytes);
                dl::image::img_t nat_img_888 = {
                    .data = nat_888,
                    .width = native_w,
                    .height = native_h,
                    .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB888,
                };
                size_t d1 = s_detector->run(nat_img_888).size();
                size_t d2 = s_detector->run(nat_img_888).size();
                heap_caps_free(nat_888);
                ESP_LOGI(TAG, "self-test black 120x160 RGB888: 1st=%lu 2nd=%lu (MSR native size, no resize)",
                         (unsigned long)d1, (unsigned long)d2);
            }
            // 诊断：读出 MSR 模型输入张量 shape/exponent/前 16 字节（run 后读，若全 0 说明预处理写了零、问题在模型）
            dl::Model* msr_model = s_detector->get_raw_model(0);
            if (msr_model) {
                dl::TensorBase* inp = msr_model->get_input();
                if (inp && inp->data) {
                    char shape_str[32] = {0};
                    int n = 0;
                    for (size_t i = 0; i < inp->shape.size() && n < (int)sizeof(shape_str) - 2; i++) {
                        n += snprintf(shape_str + n, sizeof(shape_str) - (size_t)n, "%s%d", i ? "," : "", inp->shape[i]);
                    }
                    const int8_t* p = (const int8_t*)inp->data;
                    ESP_LOGI(TAG, "MSR input: shape=[%s] exp=%d dtype=%d", shape_str, inp->exponent, (int)inp->dtype);
                    ESP_LOGI(TAG, "MSR input first 16 bytes: %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
                             p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
                             p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
                }
            }
        } else {
            ESP_LOGW(TAG, "self-test black: alloc failed, skip");
        }
    }

    // 检测输入：与 esp-who 一致，img_t 指向帧缓冲后直接 run(img)。esp-who 不做字节交换；若杂点/框错位可试 config.h FACE_DETECT_RGB565_BYTE_SWAP=1
    void* detect_src = qframe->data;
    uint8_t* swap_buf = nullptr;
#if FACE_DETECT_RGB565_BYTE_SWAP
    swap_buf = (uint8_t*)heap_caps_malloc(size_rgb565, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!swap_buf) {
        swap_buf = (uint8_t*)heap_caps_malloc(size_rgb565, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (swap_buf) {
        memcpy(swap_buf, qframe->data, size_rgb565);
        uint16_t* p = reinterpret_cast<uint16_t*>(swap_buf);
        for (size_t i = 0; i < (size_t)w * h; i++) {
            p[i] = static_cast<uint16_t>((p[i] >> 8) | (p[i] << 8));
        }
        detect_src = swap_buf;
    }
#endif

    dl::image::img_t img = {
        .data = detect_src,
        .width = w,
        .height = h,
        .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565,
    };
    auto& raw_results = s_detector->run(img);
    taskYIELD();
    // 打印原始检测结果（run 后、rescale 前），便于验证
    {
        int n = 0;
        const int max_log = 15;
        for (const auto& r : raw_results) {
            if (r.box.size() >= 4) {
                if (n < max_log) {
                    ESP_LOGI(TAG, "raw[%d] [%d,%d,%d,%d] score=%.2f",
                             n, r.box[0], r.box[1], r.box[2], r.box[3], r.score);
                }
                n++;
            }
        }
        if (n > 0) {
            ESP_LOGI(TAG, "raw total %d face(s)", n);
        }
    }
    rescale_and_filter(raw_results, w, h, img.width, img.height, out_results);

    if (swap_buf) {
        heap_caps_free(swap_buf);
    }
    return true;

#else
    (void)size_rgb565;
    ESP_LOGD(TAG, "Face detection only on ESP32-S3, skip.");
    return false;
#endif
}

}  // namespace app_ai
