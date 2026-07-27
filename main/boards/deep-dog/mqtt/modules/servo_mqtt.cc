#include "mqtt/modules/servo_mqtt.h"

#include "mqtt/mqtt_client.h"
#include "servo/servo_control.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <ctime>

#define TAG "dog_mqtt_servo"

namespace {

constexpr int64_t kStatusThrottleUs = 200000;  // 200ms

int64_t UnixTs() {
    const time_t now = time(nullptr);
    if (now > 1000000000) {
        return static_cast<int64_t>(now);
    }
    return static_cast<int64_t>(esp_timer_get_time() / 1000000LL);
}

servo_type_t ParseType(int v, bool* ok) {
    *ok = true;
    switch (v) {
        case 90:
            return SERVO_TYPE_90;
        case 180:
            return SERVO_TYPE_180;
        case 270:
            return SERVO_TYPE_270;
        case 360:
            return SERVO_TYPE_360;
        default:
            *ok = false;
            return SERVO_TYPE_180;
    }
}

}  // namespace

DeepDogServoMqtt::DeepDogServoMqtt(DeepDogMqttClient* client) : client_(client) {
    esp_timer_handle_t timer = nullptr;
    esp_timer_create_args_t args = {
        .callback = &DeepDogServoMqtt::ThrottleTimerCb,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "dog_servo_mqtt",
        .skip_unhandled_events = true,
    };
    if (esp_timer_create(&args, &timer) == ESP_OK) {
        throttle_timer_ = timer;
    }
}

DeepDogServoMqtt::~DeepDogServoMqtt() {
    Stop();
    DeepDogServoSetNotifyCallback(nullptr, nullptr);
    if (throttle_timer_) {
        esp_timer_delete(static_cast<esp_timer_handle_t>(throttle_timer_));
        throttle_timer_ = nullptr;
    }
}

void DeepDogServoMqtt::ThrottleTimerCb(void* arg) {
    auto* self = static_cast<DeepDogServoMqtt*>(arg);
    if (!self) {
        return;
    }
    if (self->pending_publish_) {
        self->pending_publish_ = false;
        self->PublishStatus(true);
    }
}

void DeepDogServoMqtt::BankNotifyCb(void* ctx) {
    auto* self = static_cast<DeepDogServoMqtt*>(ctx);
    if (self) {
        self->OnBankNotify();
    }
}

void DeepDogServoMqtt::OnBankNotify() {
    ScheduleStatusPublish();
}

void DeepDogServoMqtt::ScheduleStatusPublish() {
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

void DeepDogServoMqtt::OnConnected() {
    if (!enabled_) {
        return;
    }
    connected_ = true;
    DeepDogServoSetNotifyCallback(&DeepDogServoMqtt::BankNotifyCb, this);
    if (client_) {
        client_->Subscribe("servo/cmd", 1);
    }
    PublishStatus(true);
}

void DeepDogServoMqtt::OnDisconnected() {
    connected_ = false;
    pending_publish_ = false;
    if (throttle_timer_) {
        esp_timer_stop(static_cast<esp_timer_handle_t>(throttle_timer_));
    }
}

void DeepDogServoMqtt::Stop() {
    OnDisconnected();
    DeepDogServoSetNotifyCallback(nullptr, nullptr);
}

void DeepDogServoMqtt::OnMessage(const std::string& topic, const std::string& payload) {
    if (!enabled_ || !client_) {
        return;
    }
    if (topic != client_->Topic("servo/cmd")) {
        return;
    }

#if !DEEP_DOG_SERVO_ENABLE
    ESP_LOGW(TAG, "servo/cmd ignored (servo disabled)");
    return;
#else
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        ESP_LOGW(TAG, "servo/cmd invalid_json");
        return;
    }

    const cJSON* index_j = cJSON_GetObjectItem(root, "index");
    if (!cJSON_IsNumber(index_j)) {
        ESP_LOGW(TAG, "servo/cmd missing index");
        cJSON_Delete(root);
        return;
    }
    const int index = static_cast<int>(index_j->valuedouble);
    if (index < 0 || index >= DeepDogServoCount()) {
        ESP_LOGW(TAG, "servo/cmd bad index=%d", index);
        cJSON_Delete(root);
        return;
    }

    bool touched = false;

    const cJSON* type_j = cJSON_GetObjectItem(root, "type");
    if (cJSON_IsNumber(type_j)) {
        bool ok = false;
        const servo_type_t type = ParseType(static_cast<int>(type_j->valuedouble), &ok);
        if (!ok) {
            ESP_LOGW(TAG, "servo/cmd bad type");
        } else {
            DeepDogServoSnapshot snap {};
            const bool attached = DeepDogServoGetSnapshot(index, &snap) && snap.attached;
            if (attached) {
                DeepDogServoSetType(index, type);
            } else {
                DeepDogServoAttach(index, type);
            }
            touched = true;
        }
    }

    const cJSON* attach_j = cJSON_GetObjectItem(root, "attach");
    if (cJSON_IsBool(attach_j)) {
        if (cJSON_IsTrue(attach_j)) {
            servo_type_t type = SERVO_TYPE_180;
            DeepDogServoSnapshot snap {};
            if (DeepDogServoGetSnapshot(index, &snap) && snap.type > 0) {
                bool ok = false;
                type = ParseType(snap.type, &ok);
                if (!ok) {
                    type = SERVO_TYPE_180;
                }
            }
            if (cJSON_IsNumber(type_j)) {
                bool ok = false;
                type = ParseType(static_cast<int>(type_j->valuedouble), &ok);
                if (!ok) {
                    type = SERVO_TYPE_180;
                }
            }
            DeepDogServoAttach(index, type);
        } else {
            DeepDogServoDetach(index);
        }
        touched = true;
    }

    const cJSON* angle_j = cJSON_GetObjectItem(root, "angle");
    if (cJSON_IsNumber(angle_j)) {
        uint32_t duration_ms = 0;
        const cJSON* dur_j = cJSON_GetObjectItem(root, "duration_ms");
        if (cJSON_IsNumber(dur_j) && dur_j->valuedouble > 0) {
            duration_ms = static_cast<uint32_t>(dur_j->valuedouble);
        }
        const esp_err_t err =
            DeepDogServoSetAngle(index, static_cast<int>(angle_j->valuedouble), duration_ms);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "servo/cmd set_angle failed index=%d err=%s", index, esp_err_to_name(err));
        }
        touched = true;
    }

    cJSON_Delete(root);
    if (touched) {
        PublishStatus(true);
    }
#endif
}

bool DeepDogServoMqtt::PublishStatus(bool force) {
    if (!enabled_ || !client_ || !connected_) {
        return false;
    }
    (void)force;

    cJSON* root = cJSON_CreateObject();
    cJSON* arr = cJSON_AddArrayToObject(root, "servos");
    for (int i = 0; i < DeepDogServoCount(); ++i) {
        DeepDogServoSnapshot snap {};
        if (!DeepDogServoGetSnapshot(i, &snap)) {
            continue;
        }
        cJSON* item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "index", snap.index);
        cJSON_AddNumberToObject(item, "angle", snap.angle);
        cJSON_AddNumberToObject(item, "target", snap.target);
        cJSON_AddBoolToObject(item, "attached", snap.attached);
        cJSON_AddBoolToObject(item, "moving", snap.moving);
        cJSON_AddNumberToObject(item, "type", snap.type);
        cJSON_AddNumberToObject(item, "min", snap.min_angle);
        cJSON_AddNumberToObject(item, "max", snap.max_angle);
        cJSON_AddItemToArray(arr, item);
    }
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(UnixTs()));

    char* raw = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!raw) {
        return false;
    }
    const bool ok = client_->Publish("servo/status", raw, 0, true);
    cJSON_free(raw);
    if (ok) {
        last_publish_us_ = esp_timer_get_time();
        pending_publish_ = false;
    }
    return ok;
}
