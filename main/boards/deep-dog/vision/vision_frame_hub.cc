#include "vision/vision_frame_hub.h"

#include "vision/vision_config.h"
#if DEEP_DOG_VISION_CODEC_H264
#include "vision/h264_sw_encoder.h"
#include "vision/rtsp_h264_pusher.h"
#else
#include "vision/rtsp_jpeg_pusher.h"
#endif

#include "esp_video.h"
#include "camera.h"
#include "face_ai_bridge.h"
#include "face_ai_config.h"
#include "image_to_jpeg.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/idf_additions.h>

#include <cstring>
#include <linux/videodev2.h>

#define TAG "vision_hub"

namespace {

static v4l2_pix_fmt_t V4lFromCameraFrame(const CameraFrame& cf) {
    switch (cf.format) {
        case 1:
            return V4L2_PIX_FMT_RGB565;
        case 2:
            return V4L2_PIX_FMT_RGB24;
        case 3:
            return V4L2_PIX_FMT_YUYV;
        default:
            return static_cast<v4l2_pix_fmt_t>(cf.format);
    }
}

static bool PackedRgb565FromFrame(const CameraFrame& cf, std::vector<uint8_t>* packed) {
    const uint32_t w = cf.width;
    const uint32_t h = cf.height;
    if (w == 0 || h == 0) {
        return false;
    }
    const size_t row_b = (size_t)w * 2u;
    if (cf.len >= row_b * (size_t)h) {
        if (cf.len % (size_t)h == 0u) {
            const size_t src_stride = cf.len / (size_t)h;
            if (src_stride < row_b) {
                return false;
            }
            if (src_stride == row_b) {
                packed->assign(cf.data, cf.data + row_b * (size_t)h);
                return true;
            }
            packed->resize(row_b * (size_t)h);
            for (uint32_t row = 0; row < h; row++) {
                memcpy(packed->data() + row * row_b, cf.data + row * src_stride, row_b);
            }
            return true;
        }
        packed->assign(cf.data, cf.data + row_b * (size_t)h);
        return true;
    }
    return false;
}

}  // namespace

VisionFrameHub::VisionFrameHub(EspVideo* camera) : camera_(camera) {
#if DEEP_DOG_VISION_CODEC_H264
    pusher_ = std::make_unique<RtspH264Pusher>();
    h264_enc_ = std::make_unique<H264SwEncoder>();
#else
    pusher_ = std::make_unique<RtspJpegPusher>();
#endif
    char url[160];
    snprintf(url, sizeof(url), "rtsp://%s:%u/%s", DEEP_DOG_VISION_RTSP_HOST,
             static_cast<unsigned>(DEEP_DOG_VISION_RTSP_PORT), DEEP_DOG_VISION_STREAM_PATH);
    pusher_->SetUrl(url);
}

VisionFrameHub::~VisionFrameHub() {
    Stop();
}

void VisionFrameHub::SetRtspUrl(const std::string& url) {
    if (pusher_) {
        pusher_->SetUrl(url);
        TearDownPusher();
        reconnect_delay_ms_ = DEEP_DOG_VISION_RECONNECT_MIN_MS;
        next_reconnect_ms_ = 0;
    }
}

std::string VisionFrameHub::RtspUrl() const {
    return pusher_ ? pusher_->Url() : std::string();
}

VisionPushStatus VisionFrameHub::GetPushStatus() const {
    if (GetPublishMode() != VisionPublishMode::RtspPush) {
        return VisionPushStatus::Idle;
    }
    return pusher_ ? pusher_->Status() : VisionPushStatus::Idle;
}

void VisionFrameHub::SetPublishMode(VisionPublishMode mode) {
    const auto prev = GetPublishMode();
    mode_.store(static_cast<uint8_t>(mode), std::memory_order_release);
    if (mode != VisionPublishMode::RtspPush) {
        TearDownPusher();
        reconnect_delay_ms_ = DEEP_DOG_VISION_RECONNECT_MIN_MS;
    }
    if (mode != VisionPublishMode::HttpMjpeg && mode != prev) {
        std::lock_guard<std::mutex> lock(jpeg_mu_);
        jpeg_latest_.clear();
    }
    ESP_LOGI(TAG, "publish mode -> %s", VisionPublishModeStr(mode));
}

void VisionFrameHub::PublishJpeg(std::vector<uint8_t>&& jpeg) {
    std::lock_guard<std::mutex> lock(jpeg_mu_);
    jpeg_latest_ = std::move(jpeg);
}

bool VisionFrameHub::CopyLatestJpeg(std::vector<uint8_t>* out) const {
    std::lock_guard<std::mutex> lock(jpeg_mu_);
    if (jpeg_latest_.empty()) {
        return false;
    }
    *out = jpeg_latest_;
    return true;
}

bool VisionFrameHub::HasJpegFrame() const {
    std::lock_guard<std::mutex> lock(jpeg_mu_);
    return !jpeg_latest_.empty();
}

bool VisionFrameHub::Start() {
    if (task_) {
        return true;
    }
    if (!camera_) {
        ESP_LOGE(TAG, "no camera");
        return false;
    }
    stop_.store(false, std::memory_order_release);

#if DEEP_DOG_VISION_PUSH_AT_BOOT
    SetPublishMode(VisionPublishMode::RtspPush);
#endif

#if DEEP_DOG_VISION_CODEC_H264
    // openh264 SW encode needs a deep stack (SPIRAM task stack)
    constexpr uint32_t kStack = 49152;
#else
    constexpr uint32_t kStack = 12288;
#endif
    if (xTaskCreateWithCaps(TaskEntry, "vision_hub", kStack, this, 4, &task_,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        task_ = nullptr;
        ESP_LOGE(TAG, "task create failed");
        return false;
    }
    ESP_LOGI(TAG, "VisionFrameHub started (face always-on when enabled)");
    return true;
}

void VisionFrameHub::Stop() {
    stop_.store(true, std::memory_order_release);
    TearDownPusher();
    // 任务自删；稍等
    for (int i = 0; i < 50 && task_ != nullptr; ++i) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void VisionFrameHub::TaskEntry(void* arg) {
    static_cast<VisionFrameHub*>(arg)->TaskLoop();
}

void VisionFrameHub::TearDownPusher() {
    if (pusher_ && pusher_->IsConnected()) {
        pusher_->Disconnect();
    }
}

void VisionFrameHub::EnsurePusherConnected() {
    if (!pusher_) {
        return;
    }
    if (pusher_->IsConnected() && pusher_->Status() == VisionPushStatus::Streaming) {
        reconnect_delay_ms_ = DEEP_DOG_VISION_RECONNECT_MIN_MS;
        return;
    }
    const int64_t now = esp_timer_get_time() / 1000;
    if (now < next_reconnect_ms_) {
        return;
    }
    if (pusher_->Connect()) {
        reconnect_delay_ms_ = DEEP_DOG_VISION_RECONNECT_MIN_MS;
        next_reconnect_ms_ = 0;
        return;
    }
    next_reconnect_ms_ = now + reconnect_delay_ms_;
    reconnect_delay_ms_ = reconnect_delay_ms_ * 2;
    if (reconnect_delay_ms_ > DEEP_DOG_VISION_RECONNECT_MAX_MS) {
        reconnect_delay_ms_ = DEEP_DOG_VISION_RECONNECT_MAX_MS;
    }
}

bool VisionFrameHub::CapturePackedRgb565(std::vector<uint8_t>* packed, uint16_t* w, uint16_t* h, uint32_t* v4l_fmt) {
    if (!camera_ || !packed || !w || !h || !v4l_fmt) {
        return false;
    }
    if (!camera_->CaptureOnly()) {
        return false;
    }
    CameraFrame cf{};
    if (!camera_->GetLastFrame(&cf)) {
        return false;
    }
    const v4l2_pix_fmt_t vf = V4lFromCameraFrame(cf);
    *v4l_fmt = static_cast<uint32_t>(vf);
    *w = static_cast<uint16_t>(cf.width);
    *h = static_cast<uint16_t>(cf.height);
    if (vf == V4L2_PIX_FMT_RGB565) {
        return PackedRgb565FromFrame(cf, packed);
    }
    packed->assign(cf.data, cf.data + cf.len);
    return !packed->empty();
}

bool VisionFrameHub::EncodeJpeg(const uint8_t* rgb, size_t len, uint16_t w, uint16_t h, uint32_t v4l_fmt,
                                std::vector<uint8_t>* out) {
    if (!rgb || !out || w == 0 || h == 0) {
        return false;
    }
    uint8_t* jpeg_ptr = nullptr;
    size_t jpeg_len = 0;
    if (!image_to_jpeg(const_cast<uint8_t*>(rgb), len, w, h, static_cast<v4l2_pix_fmt_t>(v4l_fmt),
                       static_cast<uint8_t>(jpeg_quality_), &jpeg_ptr, &jpeg_len)) {
        return false;
    }
    out->assign(jpeg_ptr, jpeg_ptr + jpeg_len);
    free(jpeg_ptr);
    return true;
}

void VisionFrameHub::TaskLoop() {
    while (!stop_.load(std::memory_order_acquire)) {
        const VisionPublishMode mode = GetPublishMode();
        const bool need_publish = (mode == VisionPublishMode::HttpMjpeg || mode == VisionPublishMode::RtspPush);
#if DEEP_DOG_FACE_AI_ENABLE
        const bool face_on = DeepDogFaceAiIsEnabled();
#else
        const bool face_on = false;
#endif
        if (!need_publish && !face_on) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        std::vector<uint8_t> packed;
        uint16_t w = 0;
        uint16_t h = 0;
        uint32_t v4l = 0;
        if (!CapturePackedRgb565(&packed, &w, &h, &v4l)) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

#if DEEP_DOG_FACE_AI_ENABLE
        if (face_on && !packed.empty()) {
            DeepDogFaceAiSubmitFrameIfDue(packed.data(), packed.size(), w, h);
        }
#endif

        if (need_publish) {
            if (mode == VisionPublishMode::HttpMjpeg) {
                std::vector<uint8_t> jpeg;
                if (EncodeJpeg(packed.data(), packed.size(), w, h, v4l, &jpeg) && !jpeg.empty()) {
                    PublishJpeg(std::move(jpeg));
                }
            } else if (mode == VisionPublishMode::RtspPush) {
#if DEEP_DOG_VISION_CODEC_H264
                EnsurePusherConnected();
                if (pusher_ && pusher_->IsConnected() && h264_enc_) {
                    std::vector<uint8_t> annexb;
                    if (h264_enc_->EncodeRgb565(packed.data(), packed.size(), w, h, &annexb) &&
                        !annexb.empty()) {
                        if (!pusher_->PushAnnexB(annexb.data(), annexb.size())) {
                            EnsurePusherConnected();
                        } else {
                            static int s_ok = 0;
                            if (s_ok < 3) {
                                ESP_LOGI(TAG, "H264 push ok bytes=%u %ux%u",
                                         static_cast<unsigned>(annexb.size()), w, h);
                                ++s_ok;
                            }
                        }
                    }
                }
#else
                std::vector<uint8_t> jpeg;
                if (EncodeJpeg(packed.data(), packed.size(), w, h, v4l, &jpeg) && !jpeg.empty()) {
                    EnsurePusherConnected();
                    if (pusher_ && pusher_->IsConnected()) {
                        if (!pusher_->PushJpeg(jpeg.data(), jpeg.size(), w, h)) {
                            EnsurePusherConnected();
                        }
                    }
                    PublishJpeg(std::move(jpeg));
                }
#endif
            }
            int fps = target_fps_ > 0 ? target_fps_ : 5;
            vTaskDelay(pdMS_TO_TICKS(1000 / fps));
        } else {
            // 静默人脸：按 face 最小间隔节奏采帧
            int wait_ms = DEEP_DOG_FACE_AI_MIN_INTERVAL_MS;
            if (wait_ms < 200) {
                wait_ms = 200;
            }
            vTaskDelay(pdMS_TO_TICKS(wait_ms));
        }
    }
    task_ = nullptr;
    vTaskDeleteWithCaps(nullptr);
}
