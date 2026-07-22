#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

/**
 * ESP32-S3 esp_h264 软件编码器封装：RGB565 → I420 → Annex-B H.264。
 * 分辨率建议 ≤320×240；宽高需为 16 的倍数（240×240 可用）。
 */
class H264SwEncoder {
public:
    H264SwEncoder() = default;
    ~H264SwEncoder();

    H264SwEncoder(const H264SwEncoder&) = delete;
    H264SwEncoder& operator=(const H264SwEncoder&) = delete;

    bool Open(uint16_t width, uint16_t height, uint8_t fps);
    void Close();
    bool IsOpen() const { return enc_ != nullptr; }

    /** 编码一帧 packed RGB565；成功时 out_annexb 为带 start code 的 Annex-B */
    bool EncodeRgb565(const uint8_t* rgb565, size_t len, uint16_t width, uint16_t height,
                      std::vector<uint8_t>* out_annexb);

private:
    void* enc_ = nullptr;  // esp_h264_enc_handle_t
    uint16_t width_ = 0;
    uint16_t height_ = 0;
    uint8_t fps_ = 3;
    uint8_t* i420_buf_ = nullptr;
    uint32_t i420_len_ = 0;
    uint8_t* out_buf_ = nullptr;
    uint32_t out_cap_ = 0;
    uint32_t pts_ = 0;
};
