#include "mqtt/modules/face_mqtt.h"

#include "mqtt/mqtt_client.h"
#include "mqtt/mqtt_config.h"

#include "face_ai_bridge.h"
#include "face_ai_config.h"
#include "face_ai_types.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <cstdio>
#include <cstring>
#include <ctime>

#define TAG "dog_mqtt_face"

namespace {

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

}  // namespace

DeepDogFaceMqtt::DeepDogFaceMqtt(DeepDogMqttClient* client) : client_(client) {
    esp_timer_create_args_t args = {
        .callback = &DeepDogFaceMqtt::PollTimerCb,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "dog_face_poll",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&args, &poll_timer_);
}

DeepDogFaceMqtt::~DeepDogFaceMqtt() {
    Stop();
    if (poll_timer_) {
        esp_timer_delete(poll_timer_);
        poll_timer_ = nullptr;
    }
}

void DeepDogFaceMqtt::PollTimerCb(void* arg) {
    auto* self = static_cast<DeepDogFaceMqtt*>(arg);
    if (self) {
        self->PublishStatus(false);
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
    PublishStatus(true);
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
        if (strcmp(action->valuestring, "clear_db") == 0) {
            const bool ok = DeepDogFaceAiClearDb();
            ESP_LOGI(TAG, "face/cmd clear_db ok=%d", ok ? 1 : 0);
            touched = true;
        } else {
            ESP_LOGW(TAG, "face/cmd unknown action=%s", action->valuestring);
        }
    }

    const cJSON* en = cJSON_GetObjectItem(root, "enabled");
    if (cJSON_IsBool(en)) {
        DeepDogFaceAiSetEnabled(cJSON_IsTrue(en));
        ESP_LOGI(TAG, "face/cmd enabled=%d", cJSON_IsTrue(en) ? 1 : 0);
        touched = true;
    }

    const cJSON* pipe = cJSON_GetObjectItem(root, "pipeline");
    if (cJSON_IsString(pipe) && pipe->valuestring) {
        if (strcmp(pipe->valuestring, "identity") == 0) {
            DeepDogFaceAiSetPipeline(DeepDogFacePipeline::Identity);
            touched = true;
        } else if (strcmp(pipe->valuestring, "live") == 0) {
            DeepDogFaceAiSetPipeline(DeepDogFacePipeline::Live);
            touched = true;
        } else {
            ESP_LOGW(TAG, "face/cmd bad pipeline=%s", pipe->valuestring);
        }
    }

    const cJSON* interval = cJSON_GetObjectItem(root, "detect_interval_ms");
    if (cJSON_IsNumber(interval)) {
        DeepDogFaceAiSetDetectIntervalMs(static_cast<int>(interval->valuedouble));
        touched = true;
    }

    cJSON_Delete(root);
    if (!touched) {
        ESP_LOGW(TAG, "face/cmd empty (need enabled|action|pipeline|detect_interval_ms)");
        return;
    }
    last_fingerprint_.clear();
    PublishStatus(true);
#endif
}

bool DeepDogFaceMqtt::PublishStatus(bool force) {
    if (!enabled_ || !client_ || !client_->IsConnected()) {
        return false;
    }

#if !DEEP_DOG_FACE_AI_ENABLE
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "enabled", false);
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
    DeepDogFaceAiCopySnapshot(&snap);
    const bool user_on = DeepDogFaceAiIsEnabled();
    const DeepDogFacePipeline pipeline = DeepDogFaceAiGetPipeline();
    const int interval_ms = DeepDogFaceAiGetDetectIntervalMs();
    const bool has_person = snap.count > 0;
    const int best = BestFaceIndex(snap);

    char fingerprint[448];
    int cx_i = 0, cy_i = 0;
    float score = 0;
    if (best >= 0) {
        const DeepDogFaceBox& b = snap.faces[best];
        cx_i = static_cast<int>((b.x0 + b.x1) * 0.5f + 0.5f);
        cy_i = static_cast<int>((b.y0 + b.y1) * 0.5f + 0.5f);
        score = b.score;
    }
    snprintf(fingerprint, sizeof(fingerprint), "%d|%s|%d|%d|%d|%u|%u|%d|%d|%.2f|%d", user_on ? 1 : 0,
             DeepDogFacePipelineStr(pipeline), interval_ms, has_person ? 1 : 0, snap.count,
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
    static int s_log = 0;
    if (s_log < 5 || force) {
        ESP_LOGI(TAG, "face/status enabled=%d pipeline=%s interval=%d has_person=%d n=%d pub=%d", user_on ? 1 : 0,
                 DeepDogFacePipelineStr(pipeline), interval_ms, has_person ? 1 : 0, snap.count, ok ? 1 : 0);
    }
    ++s_log;
    cJSON_free(printed);
    return ok;
#endif
}
