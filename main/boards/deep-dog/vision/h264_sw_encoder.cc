#include "vision/h264_sw_encoder.h"

#include "vision/vision_config.h"

#include <esp_heap_caps.h>
#include <esp_log.h>

#include "esp_h264_enc_single.h"
#include "esp_h264_enc_single_sw.h"

#include <cstring>
#include <cstdlib>

#define TAG "h264_enc"

namespace {

static void Rgb565ToI420(const uint16_t* src, uint16_t w, uint16_t h, uint8_t* dst) {
    const size_t y_size = static_cast<size_t>(w) * static_cast<size_t>(h);
    uint8_t* y_plane = dst;
    uint8_t* u_plane = dst + y_size;
    uint8_t* v_plane = u_plane + (y_size / 4);

    for (uint16_t row = 0; row < h; ++row) {
        for (uint16_t col = 0; col < w; ++col) {
            const uint16_t p = src[row * w + col];
            const int r = ((p >> 11) & 0x1f) << 3;
            const int g = ((p >> 5) & 0x3f) << 2;
            const int b = (p & 0x1f) << 3;
            int y = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
            if (y < 0) {
                y = 0;
            }
            if (y > 255) {
                y = 255;
            }
            y_plane[row * w + col] = static_cast<uint8_t>(y);

            if ((row & 1) == 0 && (col & 1) == 0) {
                int u = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
                int v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
                if (u < 0) {
                    u = 0;
                }
                if (u > 255) {
                    u = 255;
                }
                if (v < 0) {
                    v = 0;
                }
                if (v > 255) {
                    v = 255;
                }
                const size_t uv_i = (row / 2) * (w / 2) + (col / 2);
                u_plane[uv_i] = static_cast<uint8_t>(u);
                v_plane[uv_i] = static_cast<uint8_t>(v);
            }
        }
    }
}

}  // namespace

H264SwEncoder::~H264SwEncoder() {
    Close();
}

void H264SwEncoder::Close() {
    if (enc_) {
        auto* handle = static_cast<esp_h264_enc_handle_t>(enc_);
        esp_h264_enc_close(handle);
        esp_h264_enc_del(handle);
        enc_ = nullptr;
    }
    if (i420_buf_) {
        heap_caps_free(i420_buf_);
        i420_buf_ = nullptr;
        i420_len_ = 0;
    }
    if (out_buf_) {
        heap_caps_free(out_buf_);
        out_buf_ = nullptr;
        out_cap_ = 0;
    }
    width_ = height_ = 0;
}

bool H264SwEncoder::Open(uint16_t width, uint16_t height, uint8_t fps) {
    Close();
    if (width < 16 || height < 16 || (width & 15) || (height & 15)) {
        ESP_LOGE(TAG, "size %ux%u must be multiples of 16", width, height);
        return false;
    }
    if (fps == 0) {
        fps = 3;
    }

    esp_h264_enc_cfg_sw_t cfg = {};
    cfg.pic_type = ESP_H264_RAW_FMT_I420;
    cfg.gop = fps;
    cfg.fps = fps;
    cfg.res.width = width;
    cfg.res.height = height;
    cfg.rc.bitrate = static_cast<uint32_t>(width) * height * fps / 150;
    if (cfg.rc.bitrate < 32000) {
        cfg.rc.bitrate = 32000;
    }
    cfg.rc.qp_min = 28;
    cfg.rc.qp_max = 40;

    esp_h264_enc_handle_t handle = nullptr;
    esp_h264_err_t err = esp_h264_enc_sw_new(&cfg, &handle);
    if (err != ESP_H264_ERR_OK || !handle) {
        ESP_LOGE(TAG, "esp_h264_enc_sw_new failed err=%d", static_cast<int>(err));
        return false;
    }
    err = esp_h264_enc_open(handle);
    if (err != ESP_H264_ERR_OK) {
        ESP_LOGE(TAG, "esp_h264_enc_open failed err=%d", static_cast<int>(err));
        esp_h264_enc_del(handle);
        return false;
    }

    const uint32_t yuv_len = static_cast<uint32_t>(width) * height * 3 / 2;
    i420_buf_ = static_cast<uint8_t*>(
        heap_caps_aligned_calloc(16, 1, yuv_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!i420_buf_) {
        ESP_LOGE(TAG, "i420 alloc failed");
        esp_h264_enc_close(handle);
        esp_h264_enc_del(handle);
        return false;
    }
    i420_len_ = yuv_len;

    out_buf_ = static_cast<uint8_t*>(
        heap_caps_aligned_calloc(16, 1, yuv_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!out_buf_) {
        ESP_LOGE(TAG, "outbuf alloc failed");
        heap_caps_free(i420_buf_);
        i420_buf_ = nullptr;
        esp_h264_enc_close(handle);
        esp_h264_enc_del(handle);
        return false;
    }
    out_cap_ = yuv_len;

    enc_ = handle;
    width_ = width;
    height_ = height;
    fps_ = fps;
    pts_ = 0;
    ESP_LOGI(TAG, "SW H.264 open %ux%u @%ufps bitrate=%u", width, height, fps,
             static_cast<unsigned>(cfg.rc.bitrate));
    return true;
}

bool H264SwEncoder::EncodeRgb565(const uint8_t* rgb565, size_t len, uint16_t width, uint16_t height,
                                 std::vector<uint8_t>* out_annexb) {
    if (!out_annexb || !rgb565) {
        return false;
    }
    if (!enc_ || width != width_ || height != height_) {
        if (!Open(width, height, fps_ ? fps_ : 3)) {
            return false;
        }
    }
    const size_t need = static_cast<size_t>(width) * height * 2;
    if (len < need) {
        ESP_LOGW(TAG, "rgb565 too small %u need %u", static_cast<unsigned>(len),
                 static_cast<unsigned>(need));
        return false;
    }

    Rgb565ToI420(reinterpret_cast<const uint16_t*>(rgb565), width, height, i420_buf_);

    esp_h264_enc_in_frame_t in = {};
    in.raw_data.buffer = i420_buf_;
    in.raw_data.len = i420_len_;
    in.pts = pts_++;

    esp_h264_enc_out_frame_t out = {};
    out.raw_data.buffer = out_buf_;
    out.raw_data.len = out_cap_;

    const esp_h264_err_t err =
        esp_h264_enc_process(static_cast<esp_h264_enc_handle_t>(enc_), &in, &out);
    if (err != ESP_H264_ERR_OK) {
        ESP_LOGW(TAG, "encode failed err=%d", static_cast<int>(err));
        return false;
    }
    if (out.length == 0) {
        ESP_LOGW(TAG, "encode produced 0 bytes");
        return false;
    }
    out_annexb->assign(out.raw_data.buffer, out.raw_data.buffer + out.length);
    return true;
}
