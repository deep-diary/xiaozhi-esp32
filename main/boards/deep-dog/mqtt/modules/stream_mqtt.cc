#include "mqtt/modules/stream_mqtt.h"

#include "mqtt/mqtt_client.h"
#include "mqtt/mqtt_config.h"

#include "board.h"
#include "camera.h"
#include "http-server/deep_dog_http_server.h"
#include "vision/vision_config.h"
#include "vision/vision_frame_hub.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <ctime>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>

#define TAG "dog_mqtt_stream"

namespace {

int64_t UnixTs() {
    const time_t now = time(nullptr);
    if (now > 1000000000) {
        return static_cast<int64_t>(now);
    }
    return static_cast<int64_t>(esp_timer_get_time() / 1000000LL);
}

std::string IsoTs(int64_t unix_s) {
    time_t t = static_cast<time_t>(unix_s);
    struct tm tm_utc {};
#if defined(_GNU_SOURCE) || defined(__NEWLIB__) || defined(__APPLE__) || defined(__linux__)
    gmtime_r(&t, &tm_utc);
#else
    struct tm* p = gmtime(&t);
    if (p) {
        tm_utc = *p;
    }
#endif
    char buf[40];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", tm_utc.tm_year + 1900, tm_utc.tm_mon + 1,
             tm_utc.tm_mday, tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec);
    return buf;
}

/** mode=rtsp_push 且 pusher 尚未 Starting 时，对外报 starting 更贴切 */
VisionPushStatus ReportState(VisionPublishMode mode, VisionPushStatus state) {
    if (mode == VisionPublishMode::RtspPush && state == VisionPushStatus::Idle) {
        return VisionPushStatus::Starting;
    }
    return state;
}

struct TakePhotoJob {
    DeepDogStreamMqtt* self = nullptr;
    char question[160] = {};
};

}  // namespace

DeepDogStreamMqtt::DeepDogStreamMqtt(DeepDogMqttClient* client) : client_(client) {
    esp_timer_create_args_t args = {
        .callback = &DeepDogStreamMqtt::PollTimerCb,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "dog_stream_poll",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&args, &poll_timer_);
}

DeepDogStreamMqtt::~DeepDogStreamMqtt() {
    Stop();
    if (poll_timer_) {
        esp_timer_delete(poll_timer_);
        poll_timer_ = nullptr;
    }
}

const char* DeepDogStreamMqtt::ProtocolModeStr(VisionPublishMode m) {
    switch (m) {
        case VisionPublishMode::HttpMjpeg:
            return "stream";
        case VisionPublishMode::RtspPush:
            return "rtsp_push";
        case VisionPublishMode::Off:
        default:
            return "off";
    }
}

bool DeepDogStreamMqtt::ParseMode(const char* s, VisionPublishMode* out) {
    if (!s || !out) {
        return false;
    }
    if (strcmp(s, "off") == 0) {
        *out = VisionPublishMode::Off;
        return true;
    }
    if (strcmp(s, "stream") == 0 || strcmp(s, "mjpeg") == 0) {
        *out = VisionPublishMode::HttpMjpeg;
        return true;
    }
    if (strcmp(s, "rtsp_push") == 0) {
        *out = VisionPublishMode::RtspPush;
        return true;
    }
    return false;
}

void DeepDogStreamMqtt::ApplyMode(VisionPublishMode mode) {
#if DEEP_DOG_HTTP_SERVER_ENABLE
    if (http_) {
        DeepDogCaptureMode cm = DeepDogCaptureMode::Off;
        switch (mode) {
            case VisionPublishMode::HttpMjpeg:
                cm = DeepDogCaptureMode::Streaming;
                break;
            case VisionPublishMode::RtspPush:
                cm = DeepDogCaptureMode::RtspPush;
                break;
            case VisionPublishMode::Off:
            default:
                cm = DeepDogCaptureMode::Off;
                break;
        }
        http_->SetCaptureMode(cm);
        return;
    }
#endif
#if DEEP_DOG_VISION_HUB_ENABLE
    if (hub_) {
        hub_->SetPublishMode(mode);
    }
#else
    (void)mode;
#endif
}

void DeepDogStreamMqtt::PollTimerCb(void* arg) {
    auto* self = static_cast<DeepDogStreamMqtt*>(arg);
    if (self) {
        self->PublishStatus(nullptr);
    }
}

void DeepDogStreamMqtt::OnConnected() {
    if (!enabled_) {
        return;
    }
    last_fingerprint_.clear();
    if (client_) {
        client_->Subscribe("stream/cmd", 1);
    }
    PublishStatus(nullptr);
    if (poll_timer_) {
        esp_timer_stop(poll_timer_);
        esp_timer_start_periodic(poll_timer_, DEEP_DOG_MQTT_STREAM_POLL_INTERVAL_US);
    }
}

void DeepDogStreamMqtt::OnDisconnected() {
    if (poll_timer_) {
        esp_timer_stop(poll_timer_);
    }
}

void DeepDogStreamMqtt::Stop() {
    OnDisconnected();
}

void DeepDogStreamMqtt::PublishPhotoResult(bool ok, const std::string& result, const char* error,
                                           int elapsed_ms) {
    if (!client_ || !client_->IsConnected()) {
        return;
    }
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", ok);
    if (ok) {
        cJSON_AddStringToObject(root, "result", result.c_str());
    } else {
        cJSON_AddStringToObject(root, "error", error ? error : "unknown");
    }
    cJSON_AddNumberToObject(root, "elapsed_ms", elapsed_ms);
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(UnixTs()));
    char* printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return;
    }
    const bool pub = client_->Publish("stream/photo", printed, 0, false);
    ESP_LOGI(TAG, "stream/photo ok=%d elapsed_ms=%d pub=%d err=%s", ok ? 1 : 0, elapsed_ms, pub ? 1 : 0,
             ok ? "" : (error ? error : ""));
    cJSON_free(printed);
}

void DeepDogStreamMqtt::TakePhotoTask(void* arg) {
    auto* job = static_cast<TakePhotoJob*>(arg);
    DeepDogStreamMqtt* self = job ? job->self : nullptr;
    const int64_t t0 = esp_timer_get_time();
    try {
        Camera* cam = Board::GetInstance().GetCamera();
        if (!cam) {
            if (self) {
                self->PublishPhotoResult(false, "", "no_camera", 0);
            }
        } else if (!cam->CaptureOnly() && !cam->Capture()) {
            if (self) {
                self->PublishPhotoResult(false, "", "capture_fail",
                                        static_cast<int>((esp_timer_get_time() - t0) / 1000));
            }
            ESP_LOGE(TAG, "take_photo capture failed");
        } else {
            ESP_LOGI(TAG, "take_photo CaptureOnly ok, Explain q=%s", job->question);
            const std::string r = cam->Explain(job->question);
            const int elapsed = static_cast<int>((esp_timer_get_time() - t0) / 1000);
            if (self) {
                self->PublishPhotoResult(true, r, nullptr, elapsed);
            }
            ESP_LOGI(TAG, "take_photo done elapsed_ms=%d", elapsed);
        }
    } catch (const std::exception& e) {
        const int elapsed = static_cast<int>((esp_timer_get_time() - t0) / 1000);
        ESP_LOGE(TAG, "take_photo failed: %s", e.what());
        if (self) {
            self->PublishPhotoResult(false, "", e.what(), elapsed);
        }
    } catch (...) {
        const int elapsed = static_cast<int>((esp_timer_get_time() - t0) / 1000);
        ESP_LOGE(TAG, "take_photo failed: unknown");
        if (self) {
            self->PublishPhotoResult(false, "", "unknown", elapsed);
        }
    }
    if (self) {
        self->photo_busy_.store(false, std::memory_order_release);
    }
    delete job;
    vTaskDelete(nullptr);
}

void DeepDogStreamMqtt::EnqueueTakePhoto(const char* question) {
    if (photo_busy_.exchange(true, std::memory_order_acq_rel)) {
        ESP_LOGW(TAG, "take_photo busy, skip");
        PublishPhotoResult(false, "", "busy", 0);
        return;
    }
    auto* job = new (std::nothrow) TakePhotoJob{};
    if (!job) {
        photo_busy_.store(false, std::memory_order_release);
        PublishPhotoResult(false, "", "oom", 0);
        return;
    }
    job->self = this;
    const char* q = (question && question[0]) ? question : "描述画面里有什么";
    strncpy(job->question, q, sizeof(job->question) - 1);
    job->question[sizeof(job->question) - 1] = '\0';

    constexpr uint32_t kStackWords = 12288;
    if (xTaskCreate(TakePhotoTask, "dog_stream_photo", kStackWords, job, 3, nullptr) != pdPASS) {
        delete job;
        photo_busy_.store(false, std::memory_order_release);
        PublishPhotoResult(false, "", "task_fail", 0);
        ESP_LOGE(TAG, "take_photo task create failed");
    } else {
        ESP_LOGI(TAG, "take_photo queued q=%s", q);
    }
}

void DeepDogStreamMqtt::OnMessage(const std::string& topic, const std::string& payload) {
    if (!enabled_ || !client_) {
        return;
    }
    if (topic != client_->Topic("stream/cmd")) {
        return;
    }

    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        last_error_ = "invalid_json";
        PublishStatus(last_error_.c_str());
        return;
    }

    const cJSON* action = cJSON_GetObjectItem(root, "action");
    if (!cJSON_IsString(action) || !action->valuestring) {
        last_error_ = "missing_action";
        cJSON_Delete(root);
        PublishStatus(last_error_.c_str());
        return;
    }

    const char* act = action->valuestring;
    std::string action_str = act ? act : "";

    if (action_str == "take_photo") {
        const cJSON* q = cJSON_GetObjectItem(root, "question");
        const char* qs = (cJSON_IsString(q) && q->valuestring) ? q->valuestring : nullptr;
        cJSON_Delete(root);
        EnqueueTakePhoto(qs);
        return;
    }

    VisionPublishMode mode = VisionPublishMode::Off;
    bool ok = false;

    if (action_str == "start") {
        mode = VisionPublishMode::RtspPush;
        const cJSON* mode_j = cJSON_GetObjectItem(root, "mode");
        if (cJSON_IsString(mode_j) && mode_j->valuestring) {
            if (!ParseMode(mode_j->valuestring, &mode)) {
                last_error_ = "bad_mode";
                cJSON_Delete(root);
                PublishStatus(last_error_.c_str());
                return;
            }
        }
        if (mode == VisionPublishMode::Off) {
            last_error_ = "start_requires_mode";
            cJSON_Delete(root);
            PublishStatus(last_error_.c_str());
            return;
        }
        ok = true;
    } else if (action_str == "stop") {
        mode = VisionPublishMode::Off;
        const cJSON* mode_j = cJSON_GetObjectItem(root, "mode");
        if (cJSON_IsString(mode_j) && mode_j->valuestring) {
            VisionPublishMode parsed = VisionPublishMode::Off;
            if (ParseMode(mode_j->valuestring, &parsed) && parsed != VisionPublishMode::Off) {
                ESP_LOGW(TAG, "stop ignores mode=%s", mode_j->valuestring);
            }
        }
        ok = true;
    } else {
        last_error_ = "unknown_action";
        cJSON_Delete(root);
        PublishStatus(last_error_.c_str());
        ESP_LOGW(TAG, "illegal action=%s (ignored)", action_str.c_str());
        return;
    }
    cJSON_Delete(root);

    if (!hub_ && !http_) {
        last_error_ = "stream_unavailable";
        PublishStatus(last_error_.c_str());
        return;
    }

    last_error_.clear();
    ApplyMode(mode);
    ESP_LOGI(TAG, "stream/cmd action=%s -> mode=%s ok=%d", action_str.c_str(), ProtocolModeStr(mode),
             ok ? 1 : 0);
    last_fingerprint_.clear();
    PublishStatus(nullptr);
}

bool DeepDogStreamMqtt::PublishStatus(const char* error_override) {
    if (!enabled_ || !client_ || !client_->IsConnected()) {
        return false;
    }

    VisionPublishMode mode = VisionPublishMode::Off;
    VisionPushStatus state = VisionPushStatus::Idle;
    std::string url = DEEP_DOG_VISION_PUBLIC_PLAY_URL;
#if DEEP_DOG_VISION_HUB_ENABLE
    if (hub_) {
        mode = hub_->GetPublishMode();
        state = ReportState(mode, hub_->GetPushStatus());
    }
#endif

    const char* error = error_override ? error_override : last_error_.c_str();
    if (state == VisionPushStatus::Error && (!error || !error[0])) {
        error = "push_error";
    }
#if DEEP_DOG_VISION_HUB_ENABLE
    // 卡在 starting（握手有/正在连、尚无 RTP）时给出可读 error
    if ((!error || !error[0]) && mode == VisionPublishMode::RtspPush &&
        state == VisionPushStatus::Starting && hub_ && hub_->LastRtpOkMs() == 0) {
        error = "no_rtp";
    }
#endif

    std::string push_url;
#if DEEP_DOG_VISION_HUB_ENABLE
    if (hub_) {
        push_url = hub_->RtspUrl();
    }
#endif
    const char* lan_url = DEEP_DOG_VISION_LAN_PLAY_URL;

    const int64_t ts = UnixTs();
    const std::string ts_iso = IsoTs(ts);

    char fingerprint[384];
    snprintf(fingerprint, sizeof(fingerprint), "%s|%s|%s|%s|%s|%s", VisionPushStatusStr(state),
             ProtocolModeStr(mode), url.c_str(), lan_url ? lan_url : "", push_url.c_str(),
             error ? error : "");
    if (error_override == nullptr && fingerprint == last_fingerprint_) {
        return true;  // no change
    }
    last_fingerprint_ = fingerprint;

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "state", VisionPushStatusStr(state));
    cJSON_AddStringToObject(root, "mode", ProtocolModeStr(mode));
    cJSON_AddStringToObject(root, "url", url.c_str());
    cJSON_AddStringToObject(root, "lan_url", lan_url ? lan_url : "");
    cJSON_AddStringToObject(root, "push_url", push_url.c_str());
    cJSON_AddStringToObject(root, "error", error ? error : "");
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(ts));
    cJSON_AddStringToObject(root, "ts_iso", ts_iso.c_str());

    char* printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return false;
    }
    const bool ok = client_->Publish("stream/status", printed, 0, true);
    ESP_LOGI(TAG, "stream/status retain state=%s mode=%s ok=%d", VisionPushStatusStr(state),
             ProtocolModeStr(mode), ok ? 1 : 0);
    cJSON_free(printed);
    return ok;
}
