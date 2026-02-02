/**
 * face_recognition.cpp：人脸检测 + 画框 + 识别预留。
 *
 * 当前链路（由 task_face_ai 驱动）：
 * 1. 检测：RunFaceDetectCore(qframe, &core_results) 得到已过滤的框（与 esp-who 模型与 rescale 对齐）。
 * 2. 画框：将 core_results 转为 FaceBox，DrawFaceBoxesOnRgb565(qframe->data) 在原图上画框。
 * 3. 识别：暂未实现（person_name 为 "unknown"）；预留 UpdateLocalDatabase、Explain 等，下一阶段接入数据库与姓名显示。
 * 4. 送入 q_ai：由 task_face_ai 在 ProcessOneFrame 返回后 xQueueSend(ctx->q_ai, &qframe)；本文件不负责入队。
 */
#include "face_recognition.hpp"
#include "face_draw.hpp"
#include "face_detect_core.hpp"
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

static constexpr int64_t FACE_EXPLAIN_COOLDOWN_AFTER_REG_MS = 5000;
static constexpr int64_t FACE_EXPLAIN_COOLDOWN_AFTER_NO_REG_MS = 5000;

static const char* TAG_FACE = "FaceRecognition";

static constexpr size_t MAX_DRAW_BOXES = 1;

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
    last_detection_boxes_.clear();

    const uint16_t w = qframe->width;
    const uint16_t h = qframe->height;

    std::vector<app_ai::FaceDetectResult> core_results;
    if (!app_ai::RunFaceDetectCore(qframe, &core_results)) {
        return;  // Frame unchanged (e.g. format unsupported); display will handle as-is
    }

    std::string person_name;
    for (size_t i = 0; i < core_results.size() && last_detection_boxes_.size() < MAX_DRAW_BOXES; i++) {
        const auto& r = core_results[i];
        FaceBox fb;
        fb.x = static_cast<int>(r.box[0]);
        fb.y = static_cast<int>(r.box[1]);
        fb.width = static_cast<int>(r.box[2] - r.box[0]);
        fb.height = static_cast<int>(r.box[3] - r.box[1]);
        fb.id = static_cast<int>(last_detection_boxes_.size());
        fb.name = last_detection_boxes_.empty() ? "?" : "";
        last_detection_boxes_.push_back(fb);
    }
    if (!last_detection_boxes_.empty()) {
        person_name = "unknown";
    }
    if (person_name.empty()) {
        SimulateLocalDetection(person_name);
    }

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
    // Detection is done by app_ai::RunFaceDetectCore (unified core). No local detector handle.
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
