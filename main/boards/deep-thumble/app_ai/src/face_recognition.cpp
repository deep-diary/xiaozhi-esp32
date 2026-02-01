#include "face_recognition.hpp"
#include "face_draw.hpp"
#include "face_explain.hpp"
#include "frame_queue.hpp"

#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <cinttypes>

#include "config.h"
#include "camera.h"
#include "display/display.h"
#include "esp_imgfx_color_convert.h"

static constexpr int64_t FACE_EXPLAIN_COOLDOWN_AFTER_REG_MS = 5000;
static constexpr int64_t FACE_EXPLAIN_COOLDOWN_AFTER_NO_REG_MS = 5000;

#if CONFIG_IDF_TARGET_ESP32S3
#include "human_face_detect.hpp"
#include "dl_image_process.hpp"
#include "dl_image_define.hpp"
#endif

static const char* TAG_FACE = "FaceRecognition";

static constexpr int FACE_DETECT_INPUT_W = 320;
static constexpr int FACE_DETECT_INPUT_H = 240;

FaceRecognition::FaceRecognition(Camera* camera, Display* display)
    : camera_(camera), display_(display) {
    faces_.reserve(MAX_FACE_COUNT);
}

void FaceRecognition::DrawOverlay(uint8_t* rgb565, uint16_t w, uint16_t h) {
    deep_thumble::DrawFaceBoxesOnRgb565(rgb565, w, h, last_detection_boxes_);
}

void FaceRecognition::ProcessOneFrame(QueuedFrame* qframe) {
    if (!qframe || !qframe->data || qframe->width == 0 || qframe->height == 0) {
        return;
    }
    InitializeIfNeeded();
    last_detection_boxes_.clear();

    const uint16_t w = qframe->width;
    const uint16_t h = qframe->height;
    const size_t size_rgb565 = (size_t)w * h * 2;

    if (qframe->format == 3) {
        uint8_t* out_buf = (uint8_t*)heap_caps_malloc(size_rgb565, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!out_buf) {
            ESP_LOGW(TAG_FACE, "ProcessOneFrame: YUYV convert alloc failed.");
            return;
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
            return;
        }
        esp_imgfx_data_t in_data = {.data = qframe->data, .data_len = qframe->len};
        esp_imgfx_data_t out_data = {.data = out_buf, .data_len = static_cast<uint32_t>(size_rgb565)};
        err = esp_imgfx_color_convert_process(handle, &in_data, &out_data);
        esp_imgfx_color_convert_close(handle);
        if (err != ESP_IMGFX_ERR_OK) {
            heap_caps_free(out_buf);
            return;
        }
        memcpy(qframe->data, out_buf, size_rgb565);
        heap_caps_free(out_buf);
        qframe->format = 1;
        qframe->len = size_rgb565;
    }

    if (qframe->format != 1) {
        return;
    }

    std::string person_name;
#if CONFIG_IDF_TARGET_ESP32S3
    if (esp_dl_detect_handle_ != nullptr) {
        dl::image::img_t src_img = {
            .data = qframe->data,
            .width = w,
            .height = h,
            .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565,
        };
        const bool already_320x240 = (w == FACE_DETECT_INPUT_W && h == FACE_DETECT_INPUT_H);
        void* dst_buf = nullptr;
        dl::image::img_t run_img = src_img;
        if (!already_320x240) {
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
        if (run_img.data != nullptr &&
            run_img.width == FACE_DETECT_INPUT_W && run_img.height == FACE_DETECT_INPUT_H) {
            const size_t num_pixels = (size_t)run_img.width * run_img.height;
            uint32_t lum_sum = 0;
            const uint16_t* rgb565 = (const uint16_t*)run_img.data;
            for (size_t i = 0; i < num_pixels; i++) {
                uint16_t p = rgb565[i];
                int r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
                lum_sum += (r * 255 / 31 + g * 255 / 63 + b * 255 / 31) / 3;
            }
            unsigned mean_lum = (unsigned)(lum_sum / num_pixels);
            if (dst_buf != nullptr) {
                heap_caps_free(dst_buf);
                dst_buf = nullptr;
            }
            if (mean_lum < FACE_DETECT_MIN_LUMINANCE) {
                ESP_LOGI(TAG_FACE, "Frame too dark (mean_lum=%u), no face.", mean_lum);
            } else {
                auto* detect = static_cast<HumanFaceDetect*>(esp_dl_detect_handle_);
                auto& results = detect->run(run_img);
                const int fw = static_cast<int>(w);
                const int fh = static_cast<int>(h);
                const float sx = static_cast<float>(fw) / FACE_DETECT_INPUT_W;
                const float sy = static_cast<float>(fh) / FACE_DETECT_INPUT_H;
                const float score_thr = FACE_DETECT_SCORE_THRESHOLD;
                const int min_box = FACE_DETECT_MIN_BOX_SIZE;
                constexpr size_t MAX_DRAW_BOXES = 1;
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
                for (const auto& r : results) {
                    if (r.score < score_thr) continue;
                    int box_w = static_cast<int>((BOX_X1(r) - BOX_X0(r)) * sx);
                    int box_h = static_cast<int>((BOX_Y1(r) - BOX_Y0(r)) * sy);
                    if (box_w >= min_box && box_h >= min_box && last_detection_boxes_.size() < MAX_DRAW_BOXES) {
                        FaceBox fb;
                        fb.x = static_cast<int>(BOX_X0(r) * sx);
                        fb.y = static_cast<int>(BOX_Y0(r) * sy);
                        fb.width = box_w;
                        fb.height = box_h;
                        fb.id = static_cast<int>(last_detection_boxes_.size());
                        fb.name = last_detection_boxes_.empty() ? "?" : "";
                        last_detection_boxes_.push_back(fb);
                    }
                }
#undef BOX_X0
#undef BOX_Y0
#undef BOX_X1
#undef BOX_Y1
                if (!last_detection_boxes_.empty()) {
                    person_name = "unknown";
                }
            }
        }
    }
    if (person_name.empty()) {
        SimulateLocalDetection(person_name);
    }
#else
    SimulateLocalDetection(person_name);
#endif

    for (size_t i = 0; i < last_detection_boxes_.size(); i++) {
        if (i == 0 && !person_name.empty()) {
            int idx = FindFaceIndexByName(person_name);
            last_detection_boxes_[i].name = person_name;
            last_detection_boxes_[i].id = (idx >= 0 && idx < static_cast<int>(faces_.size())) ? faces_[idx].id : 0;
        } else {
            last_detection_boxes_[i].name = "?";
            last_detection_boxes_[i].id = static_cast<int>(i);
        }
    }

    deep_thumble::DrawFaceBoxesOnRgb565(qframe->data, w, h, last_detection_boxes_);

    bool did_new_registration = false;
#if ENABLE_REMOTE_RECOGNITION
    bool is_connected = !explain_only_when_awake_ || explain_only_when_awake_();
    int64_t now_ms = GetTimeMs();
    int64_t cooldown_ms = last_explain_resulted_in_registration_
                             ? FACE_EXPLAIN_COOLDOWN_AFTER_REG_MS
                             : FACE_EXPLAIN_COOLDOWN_AFTER_NO_REG_MS;
    bool would_request_explain = !is_remote_recognition_in_progress_ &&
                                 (now_ms - last_explain_time_ms_ >= cooldown_ms);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (would_request_explain && is_connected && free_psram < 700 * 1024) {
        would_request_explain = false;
    }
    if (would_request_explain && is_connected && !person_name.empty()) {
        is_remote_recognition_in_progress_ = true;
        remote_recognition_start_ms_ = now_ms;
#if FACE_USE_VIRTUAL_ONLY
        person_name = SimulateRemoteRecognition();
        if (UpdateLocalDatabase(person_name)) {
            did_new_registration = true;
        }
        last_explain_time_ms_ = GetTimeMs();
#else
        std::string response_json;
        try {
            response_json = camera_->Explain(deep_thumble::FACE_EXPLAIN_QUESTION);
            last_explain_time_ms_ = GetTimeMs();
        } catch (const std::exception& e) {
            ESP_LOGW(TAG_FACE, "Explain failed: %s", e.what());
            last_explain_time_ms_ = GetTimeMs() + 60000;
        }
        std::vector<std::string> names;
        if (deep_thumble::ParseExplainFaceRecognitionResponse(response_json, &names)) {
            for (const std::string& name : names) {
                if (!name.empty() && UpdateLocalDatabase(name)) {
                    did_new_registration = true;
                    person_name = name;
                }
            }
        } else {
            person_name = SimulateRemoteRecognition();
            if (UpdateLocalDatabase(person_name)) {
                did_new_registration = true;
            }
        }
#endif
        is_remote_recognition_in_progress_ = false;
        last_explain_resulted_in_registration_ = did_new_registration;
    } else {
        if (is_remote_recognition_in_progress_) {
            int64_t now = GetTimeMs();
            if (now - remote_recognition_start_ms_ > REMOTE_RECOGNITION_TIMEOUT_MS) {
                is_remote_recognition_in_progress_ = false;
            }
        }
        if (!person_name.empty()) {
            UpdateLocalDatabase(person_name);
        }
    }
#else
    if (!person_name.empty()) {
        std::string remote_name = SimulateRemoteRecognition();
        if (!remote_name.empty()) {
            person_name = remote_name;
        }
        did_new_registration = UpdateLocalDatabase(person_name);
    }
#endif
    if (did_new_registration && !person_name.empty()) {
        ShowResultOnDisplay("已注册: " + person_name);
        if (on_new_face_registered_) {
            on_new_face_registered_(person_name);
        }
    }
}

void FaceRecognition::InitializeIfNeeded() {
#if CONFIG_IDF_TARGET_ESP32S3
    if (esp_dl_detect_handle_ == nullptr) {
        esp_dl_detect_handle_ = new HumanFaceDetect();
        ESP_LOGI(TAG_FACE, "ESP-DL HumanFaceDetect initialized (no face / blocked = no report).");
#if FACE_USE_VIRTUAL_ONLY
        ESP_LOGI(TAG_FACE, "FACE_USE_VIRTUAL_ONLY=1: Explain disabled, use virtual name only for registration.");
#endif
#if FACE_DETECT_BOX_SWAP_XY
        ESP_LOGI(TAG_FACE, "FACE_DETECT_BOX_SWAP_XY=1: box parsed as [y,x,y,x] for display correction.");
#endif
    }
#endif
}

void FaceRecognition::SimulateLocalDetection(std::string& out_person_name) {
    out_person_name = "unknown";
}

std::string FaceRecognition::SimulateRemoteRecognition() {
    static const char* kCandidates[] = {"张三", "李四", "王五", "unknown"};
    constexpr size_t kCandidateCount = sizeof(kCandidates) / sizeof(kCandidates[0]);

    int64_t now = GetTimeMs();
    int idx = static_cast<int>(now % kCandidateCount);
    std::string base_name = kCandidates[idx];

    if (base_name != "unknown") {
        ESP_LOGI(TAG_FACE, "Mock remote recognition result: %s", base_name.c_str());
        return base_name;
    }

    std::string allocated = AllocateUnknownName();
    ESP_LOGI(TAG_FACE, "Mock remote recognition result (unknown series): %s", allocated.c_str());
    return allocated;
}

bool FaceRecognition::UpdateLocalDatabase(const std::string& person_name) {
    if (person_name.empty()) {
        return false;
    }

    int64_t now = GetTimeMs();

    int index = FindFaceIndexByName(person_name);
    if (index >= 0) {
        faces_[index].last_seen_ms = now;
        ESP_LOGI(TAG_FACE, "Face seen again: id=%d name=%s", faces_[index].id, faces_[index].name.c_str());
        return false;
    }

    int slot = AllocateFaceSlot();
    if (slot < static_cast<int>(faces_.size())) {
        faces_[slot].name = person_name;
        faces_[slot].last_seen_ms = now;
        ESP_LOGI(TAG_FACE, "Replace face slot: id=%d new_name=%s", faces_[slot].id, faces_[slot].name.c_str());
        LogRegisteredList();
        return true;
    }
    FaceRecord record;
    record.id = slot;
    record.name = person_name;
    record.last_seen_ms = now;
    faces_.push_back(record);
    ESP_LOGI(TAG_FACE, "Register new face: id=%d name=%s", record.id, record.name.c_str());
    LogRegisteredList();
    return true;
}

int FaceRecognition::FindFaceIndexByName(const std::string& name) const {
    for (size_t i = 0; i < faces_.size(); ++i) {
        if (faces_[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int FaceRecognition::AllocateFaceSlot() const {
    if (faces_.size() < static_cast<size_t>(MAX_FACE_COUNT)) {
        return static_cast<int>(faces_.size());
    }

    int oldest_index = 0;
    int64_t oldest_time = faces_[0].last_seen_ms;
    for (size_t i = 1; i < faces_.size(); ++i) {
        if (faces_[i].last_seen_ms < oldest_time) {
            oldest_time = faces_[i].last_seen_ms;
            oldest_index = static_cast<int>(i);
        }
    }
    return oldest_index;
}

std::string FaceRecognition::AllocateUnknownName() const {
    auto exists = [this](const std::string& n) {
        return std::any_of(faces_.begin(), faces_.end(), [&n](const FaceRecord& r) { return r.name == n; });
    };

    if (!exists("unknown")) {
        return "unknown";
    }

    int suffix = 2;
    while (true) {
        std::string candidate = "unknown" + std::to_string(suffix);
        if (!exists(candidate)) {
            return candidate;
        }
        ++suffix;
    }
}

void FaceRecognition::LogRegisteredList() const {
    std::string list = "【";
    for (size_t i = 0; i < faces_.size(); i++) {
        if (i > 0) list += "，";
        list += faces_[i].name;
    }
    list += "】 ";
    list += std::to_string(faces_.size());
    list += "/";
    list += std::to_string(MAX_FACE_COUNT);
    ESP_LOGI(TAG_FACE, "Registered list: %s", list.c_str());
}

void FaceRecognition::ShowResultOnDisplay(const std::string& message) {
    if (!display_ || message.empty()) {
        return;
    }
    DisplayLockGuard lock(display_);
    display_->SetChatMessage("face", message.c_str());
}

int64_t FaceRecognition::GetTimeMs() {
    return esp_timer_get_time() / 1000;
}
