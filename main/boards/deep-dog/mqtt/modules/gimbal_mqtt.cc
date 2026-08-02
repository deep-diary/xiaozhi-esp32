#include "mqtt/modules/gimbal_mqtt.h"

#include "mqtt/mqtt_client.h"
#include "gimbal/Gimbal.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <cstring>
#include <ctime>

#define TAG "dog_mqtt_gimbal"

namespace {

constexpr int64_t kStatusThrottleUs = 200000;

int64_t UnixTs() {
    const time_t now = time(nullptr);
    if (now > 1000000000) {
        return static_cast<int64_t>(now);
    }
    return static_cast<int64_t>(esp_timer_get_time() / 1000000LL);
}

bool ParseDir(const char* s, gimbal_dir_t* out) {
    if (!s || !out) {
        return false;
    }
    if (strcmp(s, "left") == 0) {
        *out = GIMBAL_DIR_LEFT;
        return true;
    }
    if (strcmp(s, "right") == 0) {
        *out = GIMBAL_DIR_RIGHT;
        return true;
    }
    if (strcmp(s, "up") == 0) {
        *out = GIMBAL_DIR_UP;
        return true;
    }
    if (strcmp(s, "down") == 0) {
        *out = GIMBAL_DIR_DOWN;
        return true;
    }
    return false;
}

}  // namespace

DeepDogGimbalMqtt::DeepDogGimbalMqtt(DeepDogMqttClient* client) : client_(client) {
    esp_timer_handle_t timer = nullptr;
    esp_timer_create_args_t args = {
        .callback = &DeepDogGimbalMqtt::ThrottleTimerCb,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "dog_gimbal_mqtt",
        .skip_unhandled_events = true,
    };
    if (esp_timer_create(&args, &timer) == ESP_OK) {
        throttle_timer_ = timer;
    }
}

DeepDogGimbalMqtt::~DeepDogGimbalMqtt() {
    Stop();
    if (DeepDogGimbalReady()) {
        Gimbal_setNotifyCallback(DeepDogGimbalGet(), nullptr, nullptr);
    }
    if (throttle_timer_) {
        esp_timer_delete(static_cast<esp_timer_handle_t>(throttle_timer_));
        throttle_timer_ = nullptr;
    }
}

void DeepDogGimbalMqtt::ThrottleTimerCb(void* arg) {
    auto* self = static_cast<DeepDogGimbalMqtt*>(arg);
    if (!self) {
        return;
    }
    if (self->pending_publish_) {
        self->pending_publish_ = false;
        self->PublishStatus(true);
    }
}

void DeepDogGimbalMqtt::NotifyCb(void* ctx) {
    auto* self = static_cast<DeepDogGimbalMqtt*>(ctx);
    if (self) {
        self->OnGimbalNotify();
    }
}

void DeepDogGimbalMqtt::OnGimbalNotify() {
    ScheduleStatusPublish();
}

void DeepDogGimbalMqtt::ScheduleStatusPublish() {
    if (!enabled_ || !connected_) {
        return;
    }
    const int64_t now = esp_timer_get_time();
    if (now - last_publish_us_ >= kStatusThrottleUs) {
        PublishStatus(true);
        return;
    }
    pending_publish_ = true;
    if (throttle_timer_) {
        auto* timer = static_cast<esp_timer_handle_t>(throttle_timer_);
        esp_timer_stop(timer);
        const int64_t wait = kStatusThrottleUs - (now - last_publish_us_);
        esp_timer_start_once(timer, wait > 1000 ? wait : 1000);
    }
}

void DeepDogGimbalMqtt::OnConnected() {
    if (!enabled_) {
        return;
    }
    connected_ = true;
#if DEEP_DOG_GIMBAL_ENABLE
    if (!DeepDogGimbalReady()) {
        const esp_err_t err = DeepDogGimbalInit();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "DeepDogGimbalInit on MQTT connect failed: %s", esp_err_to_name(err));
        }
    }
    if (DeepDogGimbalReady()) {
        Gimbal_setNotifyCallback(DeepDogGimbalGet(), &DeepDogGimbalMqtt::NotifyCb, this);
    } else {
        ESP_LOGW(TAG, "gimbal still not ready after init retry");
    }
#endif
    if (client_) {
        client_->Subscribe("gimbal/cmd", 1);
    }
    PublishStatus(true);
}

void DeepDogGimbalMqtt::OnDisconnected() {
    connected_ = false;
    pending_publish_ = false;
    if (throttle_timer_) {
        esp_timer_stop(static_cast<esp_timer_handle_t>(throttle_timer_));
    }
}

void DeepDogGimbalMqtt::Stop() {
    OnDisconnected();
    if (DeepDogGimbalReady()) {
        Gimbal_setNotifyCallback(DeepDogGimbalGet(), nullptr, nullptr);
    }
}

void DeepDogGimbalMqtt::OnMessage(const std::string& topic, const std::string& payload) {
    if (!enabled_ || !client_) {
        return;
    }
    if (topic != client_->Topic("gimbal/cmd")) {
        return;
    }

#if !DEEP_DOG_GIMBAL_ENABLE
    ESP_LOGW(TAG, "gimbal/cmd ignored (gimbal disabled)");
    return;
#else
    if (!DeepDogGimbalReady()) {
        ESP_LOGW(TAG, "gimbal/cmd ignored (not ready)");
        return;
    }
    Gimbal_t* g = DeepDogGimbalGet();

    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        ESP_LOGW(TAG, "gimbal/cmd invalid_json");
        return;
    }

    bool touched = false;
    const cJSON* action_j = cJSON_GetObjectItem(root, "action");
    if (cJSON_IsString(action_j) && action_j->valuestring) {
        const char* a = action_j->valuestring;
        if (strcmp(a, "nudge_left") == 0) {
            Gimbal_nudgeLeft(g);
            touched = true;
        } else if (strcmp(a, "nudge_right") == 0) {
            Gimbal_nudgeRight(g);
            touched = true;
        } else if (strcmp(a, "nudge_up") == 0) {
            Gimbal_nudgeUp(g);
            touched = true;
        } else if (strcmp(a, "nudge_down") == 0) {
            Gimbal_nudgeDown(g);
            touched = true;
        } else if (strcmp(a, "jog_start") == 0) {
            const cJSON* dir_j = cJSON_GetObjectItem(root, "dir");
            gimbal_dir_t dir = GIMBAL_DIR_LEFT;
            if (cJSON_IsString(dir_j) && ParseDir(dir_j->valuestring, &dir)) {
                Gimbal_startJog(g, dir);
                touched = true;
            } else {
                ESP_LOGW(TAG, "jog_start missing/bad dir");
            }
        } else if (strcmp(a, "jog_stop") == 0) {
            Gimbal_stopJog(g);
            touched = true;
        } else if (strcmp(a, "pan_speed_up") == 0) {
            Gimbal_panSpeedUp(g);
            touched = true;
        } else if (strcmp(a, "pan_speed_down") == 0) {
            Gimbal_panSpeedDown(g);
            touched = true;
        } else if (strcmp(a, "tilt_speed_up") == 0) {
            Gimbal_tiltSpeedUp(g);
            touched = true;
        } else if (strcmp(a, "tilt_speed_down") == 0) {
            Gimbal_tiltSpeedDown(g);
            touched = true;
        } else if (strcmp(a, "set_speed") == 0) {
            const cJSON* ps = cJSON_GetObjectItem(root, "pan_speed");
            const cJSON* ts = cJSON_GetObjectItem(root, "tilt_speed");
            if (cJSON_IsNumber(ps)) {
                Gimbal_setPanSpeed(g, static_cast<int>(ps->valuedouble));
                touched = true;
            }
            if (cJSON_IsNumber(ts)) {
                Gimbal_setTiltSpeed(g, static_cast<int>(ts->valuedouble));
                touched = true;
            }
        } else if (strcmp(a, "stop") == 0) {
            Gimbal_stop(g);
            touched = true;
        } else {
            ESP_LOGW(TAG, "unknown action=%s", a);
        }
    }

    const cJSON* mode_j = cJSON_GetObjectItem(root, "mode");
    if (cJSON_IsString(mode_j) && mode_j->valuestring) {
        GimbalStatus_t st {};
        Gimbal_getStatus(g, &st);
        int pan = st.pan;
        int tilt = st.tilt;
        const cJSON* pan_j = cJSON_GetObjectItem(root, "pan");
        const cJSON* tilt_j = cJSON_GetObjectItem(root, "tilt");
        int speed = 0;
        const cJSON* speed_j = cJSON_GetObjectItem(root, "speed");
        if (cJSON_IsNumber(speed_j)) {
            speed = static_cast<int>(speed_j->valuedouble);
        }
        if (strcmp(mode_j->valuestring, "absolute") == 0) {
            if (cJSON_IsNumber(pan_j)) {
                pan = static_cast<int>(pan_j->valuedouble);
            }
            if (cJSON_IsNumber(tilt_j)) {
                tilt = static_cast<int>(tilt_j->valuedouble);
            }
            Gimbal_setAnglesTimed(g, pan, tilt, speed);
            touched = true;
        } else if (strcmp(mode_j->valuestring, "relative") == 0) {
            int d_pan = cJSON_IsNumber(pan_j) ? static_cast<int>(pan_j->valuedouble) : 0;
            int d_tilt = cJSON_IsNumber(tilt_j) ? static_cast<int>(tilt_j->valuedouble) : 0;
            /* speed==0 → 用当前轴速度插值；显式 ASAP 请用 absolute + speed 0 */
            const int spd = speed > 0 ? speed : (d_pan != 0 ? g->pan_speed : g->tilt_speed);
            Gimbal_moveRelative(g, d_pan, d_tilt, spd > 0 ? spd : g->pan_speed);
            touched = true;
        }
    }

    cJSON_Delete(root);
    if (touched) {
        PublishStatus(true);
    }
#endif
}

bool DeepDogGimbalMqtt::PublishStatus(bool force) {
    if (!enabled_ || !client_ || !connected_) {
        return false;
    }
    (void)force;

    GimbalStatus_t st {};
    if (!DeepDogGimbalReady() || !Gimbal_getStatus(DeepDogGimbalGet(), &st)) {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "ready", false);
        cJSON_AddNumberToObject(root, "ts", static_cast<double>(UnixTs()));
        char* raw = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (!raw) {
            return false;
        }
        const bool ok = client_->Publish("gimbal/status", raw, 0, true);
        cJSON_free(raw);
        return ok;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "pan", st.pan);
    cJSON_AddNumberToObject(root, "tilt", st.tilt);
    cJSON_AddNumberToObject(root, "pan_speed", st.pan_speed);
    cJSON_AddNumberToObject(root, "tilt_speed", st.tilt_speed);
    cJSON_AddNumberToObject(root, "step_deg", st.step_deg);
    cJSON_AddBoolToObject(root, "moving_pan", st.moving_pan);
    cJSON_AddBoolToObject(root, "moving_tilt", st.moving_tilt);
    cJSON_AddBoolToObject(root, "ready", st.ready);
    cJSON* lim_pan = cJSON_AddArrayToObject(root, "lim_pan");
    cJSON_AddItemToArray(lim_pan, cJSON_CreateNumber(st.lim_pan_min));
    cJSON_AddItemToArray(lim_pan, cJSON_CreateNumber(st.lim_pan_max));
    cJSON* lim_tilt = cJSON_AddArrayToObject(root, "lim_tilt");
    cJSON_AddItemToArray(lim_tilt, cJSON_CreateNumber(st.lim_tilt_min));
    cJSON_AddItemToArray(lim_tilt, cJSON_CreateNumber(st.lim_tilt_max));
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(UnixTs()));

    char* raw = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!raw) {
        return false;
    }
    const bool ok = client_->Publish("gimbal/status", raw, 0, true);
    cJSON_free(raw);
    if (ok) {
        last_publish_us_ = esp_timer_get_time();
        pending_publish_ = false;
    }
    return ok;
}
