/**
 * 统一人脸检测核心：与 esp-who WhoDetect 的模型与 rescale 逻辑对齐。
 * 输入 QueuedFrame，输出已映射到帧尺寸且过滤后的 FaceDetectResult 列表。
 */
#include "face_detect_core.hpp"
#include "config.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <cstring>

#if CONFIG_IDF_TARGET_ESP32S3
#include "human_face_detect.hpp"
#include "dl_image_process.hpp"
#include "dl_image_define.hpp"
#include "esp_imgfx_color_convert.h"
#endif

#define TAG "FaceDetectCore"

namespace app_ai {

#if CONFIG_IDF_TARGET_ESP32S3
static constexpr int FACE_DETECT_INPUT_W = 320;
static constexpr int FACE_DETECT_INPUT_H = 240;

// esp-who style: rescale detection result from 320x240 to frame size (inv_rescale = frame / detect)
static void rescale_and_filter(
    const std::list<dl::detect::result_t>& raw,
    uint16_t frame_w,
    uint16_t frame_h,
    std::vector<FaceDetectResult>* out_results)
{
    const float inv_rescale_x = static_cast<float>(frame_w) / FACE_DETECT_INPUT_W;
    const float inv_rescale_y = static_cast<float>(frame_h) / FACE_DETECT_INPUT_H;
    const float score_thr = FACE_DETECT_SCORE_THRESHOLD;
    const int min_box = FACE_DETECT_MIN_BOX_SIZE;

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
        if (r.score < score_thr) {
            continue;
        }
        float x0 = BOX_X0(r) * inv_rescale_x;
        float y0 = BOX_Y0(r) * inv_rescale_y;
        float x1 = BOX_X1(r) * inv_rescale_x;
        float y1 = BOX_Y1(r) * inv_rescale_y;
        // clamp to frame (esp-who limit_box equivalent)
        if (x0 < 0.f) x0 = 0.f;
        if (y0 < 0.f) y0 = 0.f;
        if (x1 > static_cast<float>(frame_w)) x1 = static_cast<float>(frame_w);
        if (y1 > static_cast<float>(frame_h)) y1 = static_cast<float>(frame_h);
        int box_w = static_cast<int>(x1 - x0);
        int box_h = static_cast<int>(y1 - y0);
        if (box_w >= min_box && box_h >= min_box) {
            FaceDetectResult fr;
            fr.box[0] = x0;
            fr.box[1] = y0;
            fr.box[2] = x1;
            fr.box[3] = y1;
            fr.score = r.score;
            out_results->push_back(fr);
        }
    }

#undef BOX_X0
#undef BOX_Y0
#undef BOX_X1
#undef BOX_Y1
}
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
    }
#endif

    if (qframe->format != 1) {
        ESP_LOGD(TAG, "Frame format not RGB565 (%d), skip.", qframe->format);
        return false;
    }

#if CONFIG_IDF_TARGET_ESP32S3
    static HumanFaceDetect* s_detector = nullptr;
    if (s_detector == nullptr) {
        s_detector = new HumanFaceDetect();
        if (s_detector == nullptr) {
            ESP_LOGE(TAG, "HumanFaceDetect alloc failed.");
            return false;
        }
    }

    // 检测用输入：若需字节对调则对副本操作，不修改 qframe->data，保证送显队列色彩正确
    void* detect_src_buf = nullptr;
    const size_t src_size = (size_t)w * h * 2;
#if FACE_DETECT_RGB565_BYTE_SWAP
    detect_src_buf = heap_caps_malloc(src_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!detect_src_buf) {
        detect_src_buf = heap_caps_malloc(src_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (detect_src_buf) {
        memcpy(detect_src_buf, qframe->data, src_size);
        for (size_t i = 0; i < (size_t)w * h; i++) {
            uint16_t* p = reinterpret_cast<uint16_t*>(detect_src_buf) + i;
            *p = static_cast<uint16_t>((*p >> 8) | (*p << 8));
        }
    }
#endif

    void* src_data = detect_src_buf ? detect_src_buf : qframe->data;
    dl::image::img_t src_img = {
        .data = src_data,
        .width = w,
        .height = h,
        .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565,
    };

    const bool already_320x240 = (w == FACE_DETECT_INPUT_W && h == FACE_DETECT_INPUT_H);
    void* dst_buf = nullptr;
    dl::image::img_t run_img = src_img;

    if (!already_320x240 && src_data != nullptr) {
        const size_t dst_size = (size_t)FACE_DETECT_INPUT_W * FACE_DETECT_INPUT_H * 2;
        dst_buf = heap_caps_malloc(dst_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!dst_buf) {
            dst_buf = heap_caps_malloc(dst_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (dst_buf) {
            dl::image::img_t dst_img = {
                .data = dst_buf,
                .width = static_cast<uint16_t>(FACE_DETECT_INPUT_W),
                .height = static_cast<uint16_t>(FACE_DETECT_INPUT_H),
                .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565,
            };
            dl::image::resize(src_img, dst_img, dl::image::DL_IMAGE_INTERPOLATE_BILINEAR);
            run_img = dst_img;
        }
    }

    if (run_img.data == nullptr ||
        run_img.width != FACE_DETECT_INPUT_W || run_img.height != FACE_DETECT_INPUT_H) {
        if (dst_buf != nullptr) {
            heap_caps_free(dst_buf);
        }
        if (detect_src_buf != nullptr) {
            heap_caps_free(detect_src_buf);
        }
        ESP_LOGD(TAG, "Resize failed or unsupported size, skip.");
        return false;
    }

    const size_t num_pixels = (size_t)run_img.width * run_img.height;
    uint32_t lum_sum = 0;
    const uint16_t* rgb565 = (const uint16_t*)run_img.data;
    for (size_t i = 0; i < num_pixels; i++) {
        uint16_t p = rgb565[i];
        int r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
        lum_sum += (r * 255 / 31 + g * 255 / 63 + b * 255 / 31) / 3;
    }
    unsigned mean_lum = (unsigned)(lum_sum / num_pixels);
    if (mean_lum < FACE_DETECT_MIN_LUMINANCE) {
        if (dst_buf != nullptr) {
            heap_caps_free(dst_buf);
        }
        if (detect_src_buf != nullptr) {
            heap_caps_free(detect_src_buf);
        }
        ESP_LOGD(TAG, "Frame too dark (mean_lum=%u), skip.", mean_lum);
        return true;  // ran pipeline but no detection
    }

    auto& raw_results = s_detector->run(run_img);
    rescale_and_filter(raw_results, w, h, out_results);

    // 检测用完后才能释放（run_img 可能指向 dst_buf）
    if (dst_buf != nullptr) {
        heap_caps_free(dst_buf);
    }
    if (detect_src_buf != nullptr) {
        heap_caps_free(detect_src_buf);
    }
    return true;

#else
    (void)size_rgb565;
    ESP_LOGD(TAG, "Face detection only on ESP32-S3, skip.");
    return false;
#endif
}

}  // namespace app_ai
