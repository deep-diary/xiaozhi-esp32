#include "mqtt/modules/track_mqtt.h"

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

#define TAG "dog_mqtt_track"

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

DeepDogTrackMqtt::DeepDogTrackMqtt(DeepDogMqttClient* client) : client_(client) {
    esp_timer_create_args_t args = {
        .callback = &DeepDogTrackMqtt::PollTimerCb,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "dog_track_poll",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&args, &poll_timer_);
}

DeepDogTrackMqtt::~DeepDogTrackMqtt() {
    Stop();
    if (poll_timer_) {
        esp_timer_delete(poll_timer_);
        poll_timer_ = nullptr;
    }
}

void DeepDogTrackMqtt::PollTimerCb(void* arg) {
    auto* self = static_cast<DeepDogTrackMqtt*>(arg);
    if (self) {
        self->PublishStatus(false);
    }
}

void DeepDogTrackMqtt::OnConnected() {
    if (!module_enabled_) {
        return;
    }
    if (client_) {
        client_->Subscribe("track/cmd", 1);
    }
    last_fingerprint_.clear();
    PublishStatus(true);
    if (poll_timer_) {
        esp_timer_stop(poll_timer_);
        esp_timer_start_periodic(poll_timer_, DEEP_DOG_MQTT_TRACK_POLL_INTERVAL_US);
    }
}

void DeepDogTrackMqtt::OnDisconnected() {
    if (poll_timer_) {
        esp_timer_stop(poll_timer_);
    }
}

void DeepDogTrackMqtt::Stop() {
    OnDisconnected();
}

void DeepDogTrackMqtt::OnMessage(const std::string& topic, const std::string& payload) {
    if (!module_enabled_ || !client_) {
        return;
    }
    if (topic != client_->Topic("track/cmd")) {
        return;
    }

    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        ESP_LOGW(TAG, "track/cmd invalid_json");
        return;
    }
    const cJSON* action = cJSON_GetObjectItem(root, "action");
    if (!cJSON_IsString(action) || !action->valuestring) {
        ESP_LOGW(TAG, "track/cmd missing action");
        cJSON_Delete(root);
        return;
    }
    const char* act = action->valuestring;
    if (strcmp(act, "enable") == 0) {
        user_tracking_ = true;
    } else if (strcmp(act, "disable") == 0) {
        user_tracking_ = false;
    } else {
        ESP_LOGW(TAG, "track/cmd unknown action=%s", act);
        cJSON_Delete(root);
        return;
    }
    cJSON_Delete(root);
    ESP_LOGI(TAG, "track/cmd action=%s -> enabled=%d", act, user_tracking_ ? 1 : 0);
    last_fingerprint_.clear();
    PublishStatus(true);
}

bool DeepDogTrackMqtt::PublishStatus(bool force) {
    if (!module_enabled_ || !client_ || !client_->IsConnected()) {
        return false;
    }

    bool face_on = false;
    bool has_primary = false;
    float cx = 0, cy = 0;
    int local_id = 0;
    const char* err = "";

#if DEEP_DOG_FACE_AI_ENABLE
    face_on = DeepDogFaceAiIsEnabled();
    DeepDogFaceSnapshot snap{};
    DeepDogFaceAiCopySnapshot(&snap);
    const int best = BestFaceIndex(snap);
    if (best >= 0) {
        has_primary = true;
        const DeepDogFaceBox& b = snap.faces[best];
        cx = (b.x0 + b.x1) * 0.5f;
        cy = (b.y0 + b.y1) * 0.5f;
        local_id = b.local_id;
    }
#else
    err = "face_unavailable";
#endif

    if (user_tracking_) {
        if (!face_on) {
            err = "face_off";
        } else if (!has_primary) {
            err = "no_target";
        }
    }

    const bool following = user_tracking_ && face_on && has_primary;

    char fingerprint[192];
    snprintf(fingerprint, sizeof(fingerprint), "%d|%d|%d|%.0f|%.0f|%d|%s", user_tracking_ ? 1 : 0,
             following ? 1 : 0, has_primary ? 1 : 0, cx, cy, local_id, err);
    if (!force && fingerprint == last_fingerprint_) {
        return true;
    }
    last_fingerprint_ = fingerprint;

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "enabled", user_tracking_);
    cJSON_AddBoolToObject(root, "following", following);
    if (has_primary) {
        cJSON* target = cJSON_CreateObject();
        cJSON_AddNumberToObject(target, "cx", cx);
        cJSON_AddNumberToObject(target, "cy", cy);
        if (local_id > 0) {
            cJSON_AddNumberToObject(target, "local_id", local_id);
        }
        cJSON_AddItemToObject(root, "target", target);
    }
    cJSON_AddStringToObject(root, "actuator", "none");
    cJSON_AddStringToObject(root, "error", err ? err : "");
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(UnixTs()));

    char* printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return false;
    }
    const bool ok = client_->Publish("track/status", printed, 0, false);
    static int s_log = 0;
    if (s_log < 5 || force) {
        ESP_LOGI(TAG, "track/status enabled=%d following=%d err=%s pub=%d", user_tracking_ ? 1 : 0,
                 following ? 1 : 0, err, ok ? 1 : 0);
    }
    ++s_log;
    cJSON_free(printed);
    return ok;
}
