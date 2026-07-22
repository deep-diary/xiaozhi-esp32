#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "vision_config.h"
#include "vision_types.h"

class EspVideo;
#if DEEP_DOG_VISION_CODEC_H264
class RtspH264Pusher;
class H264SwEncoder;
#else
class RtspJpegPusher;
#endif

/**
 * 统一采帧调度：人脸管线永驻送帧；MJPEG / RTSP Push 作为互斥消费者。
 */
class VisionFrameHub {
public:
    explicit VisionFrameHub(EspVideo* camera);
    ~VisionFrameHub();

    bool Start();
    void Stop();
    bool IsRunning() const { return task_ != nullptr; }

    void SetPublishMode(VisionPublishMode mode);
    VisionPublishMode GetPublishMode() const {
        return static_cast<VisionPublishMode>(mode_.load(std::memory_order_acquire));
    }

    void SetRtspUrl(const std::string& url);
    std::string RtspUrl() const;
    VisionPushStatus GetPushStatus() const;

    void PublishJpeg(std::vector<uint8_t>&& jpeg);
    bool CopyLatestJpeg(std::vector<uint8_t>* out) const;
    bool HasJpegFrame() const;

    int JpegQuality() const { return jpeg_quality_; }
    int TargetFps() const { return target_fps_; }

private:
    static void TaskEntry(void* arg);
    void TaskLoop();

    bool CapturePackedRgb565(std::vector<uint8_t>* packed, uint16_t* w, uint16_t* h, uint32_t* v4l_fmt);
    bool EncodeJpeg(const uint8_t* rgb, size_t len, uint16_t w, uint16_t h, uint32_t v4l_fmt,
                    std::vector<uint8_t>* out);
    void EnsurePusherConnected();
    void TearDownPusher();

    EspVideo* camera_;
    TaskHandle_t task_ = nullptr;
    std::atomic<bool> stop_{false};
    std::atomic<uint8_t> mode_{static_cast<uint8_t>(VisionPublishMode::Off)};

    int jpeg_quality_ = DEEP_DOG_VISION_JPEG_QUALITY;
    int target_fps_ = DEEP_DOG_VISION_PUSH_FPS;

    mutable std::mutex jpeg_mu_;
    std::vector<uint8_t> jpeg_latest_;

#if DEEP_DOG_VISION_CODEC_H264
    std::unique_ptr<RtspH264Pusher> pusher_;
    std::unique_ptr<H264SwEncoder> h264_enc_;
#else
    std::unique_ptr<RtspJpegPusher> pusher_;
#endif
    uint32_t reconnect_delay_ms_ = DEEP_DOG_VISION_RECONNECT_MIN_MS;
    int64_t next_reconnect_ms_ = 0;
};
