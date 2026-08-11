#include "mqtt/modules/face_mqtt.h"

#include "mqtt/mqtt_client.h"
#include "mqtt/mqtt_config.h"

#include "face_ai_config.h"
#include "face_ai_types.h"
#include "face_control.h"
#include "immich_client.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>

#define TAG "dog_mqtt_face"

namespace {

DeepDogFaceMqtt* s_immich_status_target = nullptr;

int64_t UnixTs() {
    const time_t now = time(nullptr);
    if (now > 1000000000) {
        return static_cast<int64_t>(now);
    }
    return static_cast<int64_t>(esp_timer_get_time() / 1000000LL);
}

int BestFaceIndex(const DeepDogFaceSnapshot& snap) {
    int best = -1;
    float best_score = -1.f;
    for (int i = 0; i < snap.count && i < 8; ++i) {
        if (snap.faces[i].score > best_score) {
            best_score = snap.faces[i].score;
            best = i;
        }
    }
    return best;
}

bool HandleCmdAction(const char* action, cJSON* root) {
    if (!action) {
        return false;
    }
    if (strcmp(action, "clear_db") == 0) {
        const bool ok = DeepDogFaceControlClearAll();
        ESP_LOGI(TAG, "face/cmd clear_db ok=%d", ok ? 1 : 0);
        return true;
    }
    const cJSON* lid = cJSON_GetObjectItem(root, "local_id");
    const int local_id = cJSON_IsNumber(lid) ? lid->valueint : 0;
    if (strcmp(action, "rename") == 0) {
        const cJSON* name = cJSON_GetObjectItem(root, "display_name");
        if (local_id <= 0 || !cJSON_IsString(name) || !name->valuestring) {
            ESP_LOGW(TAG, "face/cmd rename bad args");
            return false;
        }
        const bool ok = DeepDogFaceControlRename(local_id, name->valuestring);
        ESP_LOGI(TAG, "face/cmd rename id=%d ok=%d", local_id, ok ? 1 : 0);
        return ok;
    }
    if (strcmp(action, "delete_one") == 0) {
        if (local_id <= 0) {
            return false;
        }
        const bool ok = DeepDogFaceControlDeleteOne(local_id);
        ESP_LOGI(TAG, "face/cmd delete_one id=%d ok=%d", local_id, ok ? 1 : 0);
        return ok;
    }
    if (strcmp(action, "merge") == 0) {
        const cJSON* tgt = cJSON_GetObjectItem(root, "target_local_id");
        const int target_id = cJSON_IsNumber(tgt) ? tgt->valueint : 0;
        if (local_id <= 0 || target_id <= 0) {
            return false;
        }
        const bool ok = DeepDogFaceControlMergeAlias(local_id, target_id);
        ESP_LOGI(TAG, "face/cmd merge %d->%d ok=%d", local_id, target_id, ok ? 1 : 0);
        return ok;
    }
    if (strcmp(action, "refresh_immich") == 0) {
        const bool ok = DeepDogFaceControlRefreshImmich(local_id);
        ESP_LOGI(TAG, "face/cmd refresh_immich id=%d ok=%d", local_id, ok ? 1 : 0);
        return ok;
    }
    if (strcmp(action, "set_immich_config") == 0) {
        const cJSON* url = cJSON_GetObjectItem(root, "api_url");
        const cJSON* key = cJSON_GetObjectItem(root, "api_key");
        const cJSON* del = cJSON_GetObjectItem(root, "delete_asset");
        const char* url_s = cJSON_IsString(url) ? url->valuestring : nullptr;
        const char* key_s = cJSON_IsString(key) ? key->valuestring : nullptr;
        int delete_asset = -1;
        if (cJSON_IsNumber(del)) {
            delete_asset = del->valueint ? 1 : 0;
        }
        const bool ok = DeepDogImmichSetConfig(url_s, key_s, delete_asset);
        ESP_LOGI(TAG, "face/cmd set_immich_config ok=%d", ok ? 1 : 0);
        return ok;
    }
    if (strcmp(action, "ping_immich") == 0) {
        const bool ok = DeepDogImmichPingServer();
        ESP_LOGI(TAG, "face/cmd ping_immich ok=%d", ok ? 1 : 0);
        return true;
    }
    if (strcmp(action, "refresh_status") == 0) {
        ESP_LOGI(TAG, "face/cmd refresh_status");
        return true;
    }
    ESP_LOGW(TAG, "face/cmd unknown action=%s", action);
    return false;
}

}  // namespace

DeepDogFaceMqtt::DeepDogFaceMqtt(DeepDogMqttClient* client) : client_(client) {
    s_immich_status_target = this;
    esp_timer_create_args_t args = {
        .callback = &DeepDogFaceMqtt::PollTimerCb,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "dog_face_poll",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&args, &poll_timer_);
    esp_timer_create_args_t reg_args = {
        .callback = &DeepDogFaceMqtt::RegistryPublishTimerCb,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "dog_face_reg_pub",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&reg_args, &registry_publish_timer_);
}

DeepDogFaceMqtt::~DeepDogFaceMqtt() {
    if (s_immich_status_target == this) {
        s_immich_status_target = nullptr;
    }
    Stop();
    if (registry_publish_timer_) {
        esp_timer_stop(registry_publish_timer_);
        esp_timer_delete(registry_publish_timer_);
        registry_publish_timer_ = nullptr;
    }
    if (poll_timer_) {
        esp_timer_delete(poll_timer_);
        poll_timer_ = nullptr;
    }
}

void DeepDogFaceMqtt::ScheduleRegistryPublish() {
    if (!registry_publish_timer_) {
        return;
    }
    esp_timer_stop(registry_publish_timer_);
    esp_timer_start_once(registry_publish_timer_, 0);
}

void DeepDogFaceMqtt::RegistryPublishTimerCb(void* arg) {
    auto* self = static_cast<DeepDogFaceMqtt*>(arg);
    if (self) {
        self->PublishRegistry(true);
    }
}

void DeepDogFaceMqtt::InitRegistryHook() {
#if DEEP_DOG_FACE_AI_ENABLE
    DeepDogFaceControlSetRegistryChangedCallback([this]() {
        ScheduleRegistryPublish();
    });
#if DEEP_DOG_FACE_IMMICH_ENABLE
    DeepDogImmichSetStatusChangedCallback(+[]() {
        if (s_immich_status_target) {
            s_immich_status_target->PublishImmichStatus(true);
        }
    });
#endif
#endif
}

void DeepDogFaceMqtt::PollTimerCb(void* arg) {
    auto* self = static_cast<DeepDogFaceMqtt*>(arg);
    if (self) {
        self->status_poll_n_++;
        const bool heartbeat =
            self->status_poll_n_ >= static_cast<uint32_t>(DEEP_DOG_MQTT_FACE_STATUS_HEARTBEAT_POLLS);
        if (heartbeat) {
            self->status_poll_n_ = 0;
        }
        self->PublishStatus(heartbeat);
        self->immich_ping_every_n_++;
        if (self->immich_ping_every_n_ >= 120) {
            self->immich_ping_every_n_ = 0;
#if DEEP_DOG_FACE_IMMICH_ENABLE
            (void)DeepDogImmichPingServer();
#endif
        }
    }
}

void DeepDogFaceMqtt::OnConnected() {
    if (!enabled_) {
        return;
    }
    if (client_) {
        client_->Subscribe("face/cmd", 1);
    }
    last_fingerprint_.clear();
    last_registry_fp_.clear();
    last_immich_fp_.clear();
    immich_ping_every_n_ = 0;
    status_poll_n_ = 0;
    PublishStatus(true);
    PublishRegistry(true);
#if DEEP_DOG_FACE_IMMICH_ENABLE
    (void)DeepDogImmichPingServer();
    PublishImmichStatus(true);
#endif
    if (poll_timer_) {
        esp_timer_stop(poll_timer_);
        esp_timer_start_periodic(poll_timer_, DEEP_DOG_MQTT_FACE_POLL_INTERVAL_US);
    }
}

void DeepDogFaceMqtt::OnDisconnected() {
    if (poll_timer_) {
        esp_timer_stop(poll_timer_);
    }
}

void DeepDogFaceMqtt::Stop() {
    OnDisconnected();
}

void DeepDogFaceMqtt::OnMessage(const std::string& topic, const std::string& payload) {
    if (!enabled_ || !client_) {
        return;
    }
    if (topic != client_->Topic("face/cmd")) {
        return;
    }

#if !DEEP_DOG_FACE_AI_ENABLE
    ESP_LOGW(TAG, "face/cmd ignored (face_ai disabled at compile)");
    return;
#else
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        ESP_LOGW(TAG, "face/cmd invalid_json");
        return;
    }

    bool touched = false;
    const cJSON* action = cJSON_GetObjectItem(root, "action");
    if (cJSON_IsString(action) && action->valuestring) {
        if (HandleCmdAction(action->valuestring, root)) {
            touched = true;
        }
    }

    const cJSON* en = cJSON_GetObjectItem(root, "enabled");
    if (cJSON_IsBool(en)) {
        DeepDogFaceControlSetDetectionEnabled(cJSON_IsTrue(en));
        ESP_LOGI(TAG, "face/cmd enabled=%d", cJSON_IsTrue(en) ? 1 : 0);
        touched = true;
    }

    const cJSON* recog = cJSON_GetObjectItem(root, "recognize_enabled");
    if (cJSON_IsBool(recog)) {
        DeepDogFaceControlSetRecognitionEnabled(cJSON_IsTrue(recog));
        ESP_LOGI(TAG, "face/cmd recognize_enabled=%d", cJSON_IsTrue(recog) ? 1 : 0);
        touched = true;
    }

    const cJSON* pipe = cJSON_GetObjectItem(root, "pipeline");
    if (cJSON_IsString(pipe) && pipe->valuestring) {
        if (strcmp(pipe->valuestring, "identity") == 0) {
            DeepDogFaceControlSetPipeline(DeepDogFacePipeline::Identity);
            touched = true;
        } else if (strcmp(pipe->valuestring, "live") == 0) {
            DeepDogFaceControlSetPipeline(DeepDogFacePipeline::Live);
            touched = true;
        } else {
            ESP_LOGW(TAG, "face/cmd bad pipeline=%s", pipe->valuestring);
        }
    }

    const cJSON* interval = cJSON_GetObjectItem(root, "detect_interval_ms");
    if (cJSON_IsNumber(interval)) {
        DeepDogFaceControlSetDetectIntervalMs(static_cast<int>(interval->valuedouble));
        touched = true;
    }

#if DEEP_DOG_FACE_IMMICH_ENABLE
    const cJSON* api_url = cJSON_GetObjectItem(root, "api_url");
    const cJSON* api_key = cJSON_GetObjectItem(root, "api_key");
    const cJSON* del_asset = cJSON_GetObjectItem(root, "delete_asset");
    if (cJSON_IsString(api_url) || cJSON_IsString(api_key) || cJSON_IsNumber(del_asset)) {
        const char* url_s = cJSON_IsString(api_url) ? api_url->valuestring : nullptr;
        const char* key_s = cJSON_IsString(api_key) ? api_key->valuestring : nullptr;
        int delete_asset = -1;
        if (cJSON_IsNumber(del_asset)) {
            delete_asset = del_asset->valueint ? 1 : 0;
        }
        if (DeepDogImmichSetConfig(url_s, key_s, delete_asset)) {
            touched = true;
        }
    }
#endif

    cJSON_Delete(root);
    if (!touched) {
        ESP_LOGW(TAG, "face/cmd empty");
        return;
    }
    last_fingerprint_.clear();
    PublishStatus(true);
    ScheduleRegistryPublish();
#endif
}

void DeepDogFaceMqtt::MaybePublishPersonActive(const DeepDogFaceSnapshot& snap) {
    if (!client_ || !client_->IsConnected()) {
        return;
    }
    const int best = BestFaceIndex(snap);
    int active_id = 0;
    const char* display = "";
    if (best >= 0 && snap.faces[best].local_id > 0) {
        active_id = snap.faces[best].local_id;
        display = snap.faces[best].display_name;
    }
    if (active_id == last_person_active_id_) {
        return;
    }
    last_person_active_id_ = active_id;

    cJSON* root = cJSON_CreateObject();
    if (active_id > 0) {
        cJSON_AddNumberToObject(root, "local_id", active_id);
        if (display && display[0]) {
            cJSON_AddStringToObject(root, "display_name", display);
        }
    } else {
        cJSON_AddNumberToObject(root, "local_id", 0);
    }
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(UnixTs()));
    char* printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (printed) {
        client_->Publish("person/active", printed, 0, true);
        cJSON_free(printed);
    }
}

bool DeepDogFaceMqtt::PublishRegistry(bool force) {
    if (!enabled_ || !client_ || !client_->IsConnected()) {
        return false;
    }
#if !DEEP_DOG_FACE_AI_ENABLE
    return client_->Publish("face/registry", "{\"version\":1,\"count\":0,\"entries\":[],\"ts\":0}", 0, true);
#else
    auto buf = std::make_unique<char[]>(4096);
    const size_t n = DeepDogFaceControlFormatRegistryJson(buf.get(), 4096);
    if (n == 0) {
        ESP_LOGW(TAG, "PublishRegistry: FormatRegistryJson returned 0 (recognizer not ready?)");
        return false;
    }
    const std::string payload(buf.get(), n);
    if (!force && last_registry_fp_ == payload) {
        return true;
    }
    last_registry_fp_ = payload;
    return client_->Publish("face/registry", payload.c_str(), 0, true);
#endif
}

bool DeepDogFaceMqtt::PublishImmichStatus(bool force) {
    if (!enabled_ || !client_ || !client_->IsConnected()) {
        return false;
    }
#if !DEEP_DOG_FACE_IMMICH_ENABLE
    return client_->Publish("face/immich/status",
                            "{\"configured\":false,\"url\":\"\",\"key_len\":0,\"delete_asset\":0,"
                            "\"server_ok\":false,\"server_http\":0,\"ping_ms\":-1,\"inflight\":0,"
                            "\"last\":\"disabled\",\"last_local_id\":0,\"ts\":0}",
                            0, true);
#else
    char buf[512];
    const size_t n = DeepDogImmichFormatStatusJson(buf, sizeof(buf));
    if (n == 0) {
        return false;
    }
    if (!force && last_immich_fp_ == buf) {
        return true;
    }
    last_immich_fp_ = buf;
    return client_->Publish("face/immich/status", buf, 0, true);
#endif
}

bool DeepDogFaceMqtt::PublishStatus(bool force) {
    if (!enabled_ || !client_ || !client_->IsConnected()) {
        return false;
    }

#if !DEEP_DOG_FACE_AI_ENABLE
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "enabled", false);
    cJSON_AddBoolToObject(root, "recognize_enabled", false);
    cJSON_AddStringToObject(root, "pipeline", "live");
    cJSON_AddNumberToObject(root, "detect_interval_ms", DEEP_DOG_FACE_AI_MIN_INTERVAL_MS);
    cJSON_AddBoolToObject(root, "has_person", false);
    cJSON_AddNumberToObject(root, "n", 0);
    cJSON_AddNumberToObject(root, "w", 0);
    cJSON_AddNumberToObject(root, "h", 0);
    cJSON_AddItemToObject(root, "faces", cJSON_CreateArray());
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(UnixTs()));
    char* printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return false;
    }
    const bool ok = client_->Publish("face/status", printed, 0, false);
    cJSON_free(printed);
    return ok;
#else
    DeepDogFaceSnapshot snap{};
    DeepDogFaceControlCopySnapshot(&snap);
    const bool user_on = DeepDogFaceControlIsDetectionEnabled();
    const bool recog_on = DeepDogFaceControlIsRecognitionEnabled();
    const DeepDogFacePipeline pipeline = DeepDogFaceControlGetPipeline();
    const int interval_ms = DeepDogFaceControlGetDetectIntervalMs();
    const bool has_person = snap.count > 0;
    const int best = BestFaceIndex(snap);

    char fingerprint[512];
    int cx_i = 0, cy_i = 0;
    float score = 0;
    if (best >= 0) {
        const DeepDogFaceBox& b = snap.faces[best];
        cx_i = static_cast<int>((b.x0 + b.x1) * 0.5f + 0.5f);
        cy_i = static_cast<int>((b.y0 + b.y1) * 0.5f + 0.5f);
        score = b.score;
    }
    snprintf(fingerprint, sizeof(fingerprint), "%d|%d|%s|%d|%d|%d|%u|%u|%d|%d|%.2f|%d", user_on ? 1 : 0,
             recog_on ? 1 : 0, DeepDogFacePipelineStr(pipeline), interval_ms, has_person ? 1 : 0, snap.count,
             (unsigned)snap.frame_w, (unsigned)snap.frame_h, cx_i, cy_i, score,
             best >= 0 ? snap.faces[best].local_id : 0);
    for (int i = 0; i < snap.count && i < 8; ++i) {
        char bit[48];
        snprintf(bit, sizeof(bit), "|%d:%.0f,%.0f,%.0f,%.0f", snap.faces[i].local_id, snap.faces[i].x0,
                 snap.faces[i].y0, snap.faces[i].x1, snap.faces[i].y1);
        if (strlen(fingerprint) + strlen(bit) < sizeof(fingerprint) - 1) {
            strcat(fingerprint, bit);
        }
    }

    if (!force && fingerprint == last_fingerprint_) {
        return true;
    }
    last_fingerprint_ = fingerprint;

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "enabled", user_on);
    cJSON_AddBoolToObject(root, "recognize_enabled", recog_on);
    cJSON_AddStringToObject(root, "pipeline", DeepDogFacePipelineStr(pipeline));
    cJSON_AddNumberToObject(root, "detect_interval_ms", interval_ms);
    cJSON_AddBoolToObject(root, "has_person", has_person);
    cJSON_AddNumberToObject(root, "n", snap.count);
    cJSON_AddNumberToObject(root, "w", snap.frame_w);
    cJSON_AddNumberToObject(root, "h", snap.frame_h);

    if (best >= 0) {
        const DeepDogFaceBox& b = snap.faces[best];
        cJSON* primary = cJSON_CreateObject();
        cJSON_AddNumberToObject(primary, "cx", (b.x0 + b.x1) * 0.5f);
        cJSON_AddNumberToObject(primary, "cy", (b.y0 + b.y1) * 0.5f);
        cJSON_AddNumberToObject(primary, "score", b.score);
        cJSON_AddItemToObject(root, "primary", primary);
    }

    cJSON* faces = cJSON_CreateArray();
    for (int i = 0; i < snap.count && i < 8; ++i) {
        const DeepDogFaceBox& b = snap.faces[i];
        cJSON* f = cJSON_CreateObject();
        cJSON_AddNumberToObject(f, "x0", b.x0);
        cJSON_AddNumberToObject(f, "y0", b.y0);
        cJSON_AddNumberToObject(f, "x1", b.x1);
        cJSON_AddNumberToObject(f, "y1", b.y1);
        cJSON_AddNumberToObject(f, "cx", (b.x0 + b.x1) * 0.5f);
        cJSON_AddNumberToObject(f, "cy", (b.y0 + b.y1) * 0.5f);
        cJSON_AddNumberToObject(f, "score", b.score);
        if (b.local_id > 0) {
            cJSON_AddNumberToObject(f, "local_id", b.local_id);
        }
        if (b.display_name[0]) {
            cJSON_AddStringToObject(f, "display_name", b.display_name);
        }
        cJSON_AddItemToArray(faces, f);
    }
    cJSON_AddItemToObject(root, "faces", faces);
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(UnixTs()));

    char* printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return false;
    }
    const bool ok = client_->Publish("face/status", printed, 0, false);
    cJSON_free(printed);
    MaybePublishPersonActive(snap);
    return ok;
#endif
}
