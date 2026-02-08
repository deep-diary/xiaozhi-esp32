#include "app_ai_context.hpp"
#include "config.h"
#include "display/display.h"
#include "display/lvgl_display/lvgl_display.h"
#include "display/lvgl_display/lvgl_image.h"
#include "frame_queue.hpp"
#include "esp_imgfx_color_convert.h"
#include "esp_heap_caps.h"

#include <esp_log.h>
#include <memory>
#include <cstring>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#define TAG "app_ai"

// format 与 frame_queue/face_recognition 一致：1=RGB565，3=YUYV（V4L2 四字符码的约定）
namespace app_ai {
namespace detail {

// q_ai 保持相机原格式（YUYV/RGB565 等）；显示时按 format 转成 LVGL 可用的 RGB565，与 MCP/Explain 侧“原格式 + 按需转换”一致
void ShowQueuedFrameOnDisplay(AppAIContext* ctx, QueuedFrame* qframe) {
    if (!ctx || !ctx->display || !qframe || !qframe->data || qframe->width == 0 || qframe->height == 0) {
        return;
    }
    LvglDisplay* lvgl = dynamic_cast<LvglDisplay*>(ctx->display);
    if (!lvgl) {
        return;
    }
    const uint16_t w = qframe->width;
    const uint16_t h = qframe->height;
    const size_t size_rgb565 = (size_t)w * h * 2;
    size_t stride = ((w * 2) + 3) & ~3;
    uint8_t* buf = (uint8_t*)heap_caps_malloc(size_rgb565, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        return;
    }

    if (qframe->format == 1) {
        // RGB565：直接拷贝
        if (qframe->len >= size_rgb565) {
            memcpy(buf, qframe->data, size_rgb565);
        } else {
            heap_caps_free(buf);
            return;
        }
    } else if (qframe->format == 3) {
        // YUYV：转 RGB565 再显示（与 Capture 内预览逻辑一致）
        esp_imgfx_color_convert_cfg_t cfg = {
            .in_res = {.width = static_cast<int16_t>(w), .height = static_cast<int16_t>(h)},
            .in_pixel_fmt = ESP_IMGFX_PIXEL_FMT_YUYV,
            .out_pixel_fmt = ESP_IMGFX_PIXEL_FMT_RGB565_LE,
            .color_space_std = ESP_IMGFX_COLOR_SPACE_STD_BT601,
        };
        esp_imgfx_color_convert_handle_t handle = nullptr;
        if (esp_imgfx_color_convert_open(&cfg, &handle) != ESP_IMGFX_ERR_OK || !handle) {
            heap_caps_free(buf);
            return;
        }
        esp_imgfx_data_t in_data = {.data = qframe->data, .data_len = static_cast<uint32_t>(qframe->len)};
        esp_imgfx_data_t out_data = {.data = buf, .data_len = static_cast<uint32_t>(size_rgb565)};
        esp_imgfx_err_t err = esp_imgfx_color_convert_process(handle, &in_data, &out_data);
        esp_imgfx_color_convert_close(handle);
        if (err != ESP_IMGFX_ERR_OK) {
            heap_caps_free(buf);
            return;
        }
    } else {
        heap_caps_free(buf);
        return;
    }

    std::unique_ptr<LvglImage> image =
        std::make_unique<LvglAllocatedImage>(buf, size_rgb565, w, h, (int)stride, LV_COLOR_FORMAT_RGB565);
    DisplayLockGuard lock(ctx->display);
    lvgl->SetPreviewImage(std::move(image));
}

}  // namespace detail
}  // namespace app_ai
