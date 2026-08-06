#include "mqtt/modules/motor_mqtt.h"

#include "mqtt/mqtt_client.h"
#include "config.h"

#if DEEP_DOG_MOTOR_ENABLE
#include "motor/deep_motor.h"
#include "motor/protocol_motor.h"
#include "motor/motor_access.h"
#endif

#include <cJSON.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

#define TAG "dog_mqtt_motor"

namespace {

int64_t UnixTs() {
    const time_t now = time(nullptr);
    if (now > 1000000000) {
        return static_cast<int64_t>(now);
    }
    return static_cast<int64_t>(esp_timer_get_time() / 1000000LL);
}

float ClampPos(float v) {
#if DEEP_DOG_MOTOR_ENABLE
    return std::max(P_MIN, std::min(P_MAX, v));
#else
    return v;
#endif
}

#if DEEP_DOG_MOTOR_ENABLE
const char* ModeStatusString(motor_mode_t mode) {
    switch (mode) {
        case MOTOR_MODE_RESET:
            return "reset";
        case MOTOR_MODE_CALIBRATE:
            return "calibrate";
        case MOTOR_MODE_RUN:
            return "run";
        default:
            return "unknown";
    }
}

cJSON* BuildMotorStatusJson(uint8_t mid, const motor_status_t& st, DeepMotor* motor) {
    cJSON* m = cJSON_CreateObject();
    if (!m) {
        return nullptr;
    }
    cJSON_AddNumberToObject(m, "id", mid);
    cJSON_AddNumberToObject(m, "master_id", st.master_id);
    cJSON_AddNumberToObject(m, "position_rad", st.current_angle);
    cJSON_AddNumberToObject(m, "speed_rad_s", st.current_speed);
    cJSON_AddNumberToObject(m, "torque_nm", st.current_torque);
    cJSON_AddNumberToObject(m, "temperature", st.current_temp);
    cJSON_AddBoolToObject(m, "fault", st.error_status != 0);
    cJSON_AddNumberToObject(m, "error_status", st.error_status);
    cJSON_AddBoolToObject(m, "hall_error", st.hall_error != 0);
    cJSON_AddBoolToObject(m, "magnet_error", st.magnet_error != 0);
    cJSON_AddBoolToObject(m, "temp_error", st.temp_error != 0);
    cJSON_AddBoolToObject(m, "current_error", st.current_error != 0);
    cJSON_AddBoolToObject(m, "voltage_error", st.voltage_error != 0);
    cJSON_AddStringToObject(m, "mode_status", ModeStatusString(st.mode_status));
    cJSON_AddBoolToObject(m, "has_feedback", st.has_feedback);
    cJSON_AddNumberToObject(m, "feedback_seq", static_cast<double>(st.feedback_seq));
    cJSON_AddNumberToObject(m, "max_abs_torque", st.max_abs_torque);
    cJSON_AddBoolToObject(m, "collision", st.collision);
    cJSON_AddBoolToObject(m, "has_device_id", st.has_device_id);
    if (motor) {
        float target = 0.f;
        if (motor->getMotorTargetAngle(mid, &target)) {
            cJSON_AddNumberToObject(m, "target_rad", target);
        }
    }
    if (st.has_device_id) {
        char uid_hex[17];
        snprintf(uid_hex, sizeof(uid_hex), "%016llX", (unsigned long long)st.mcu_uid);
        cJSON_AddStringToObject(m, "mcu_uid", uid_hex);
    }
    if (st.version[0] != '\0') {
        cJSON_AddStringToObject(m, "version", st.version);
    }
    return m;
}
#endif

}  // namespace

DeepDogMotorMqtt::DeepDogMotorMqtt(DeepDogMqttClient* client) : client_(client) {
#if DEEP_DOG_MOTOR_ENABLE
    esp_timer_handle_t timer = nullptr;
    esp_timer_create_args_t args = {
        .callback = &DeepDogMotorMqtt::StatusTimerCb,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "dog_motor_mqtt",
        .skip_unhandled_events = true,
    };
    if (esp_timer_create(&args, &timer) == ESP_OK) {
        status_timer_ = timer;
    }
#endif
}

DeepDogMotorMqtt::~DeepDogMotorMqtt() {
    Stop();
#if DEEP_DOG_MOTOR_ENABLE
    if (status_timer_) {
        esp_timer_delete(static_cast<esp_timer_handle_t>(status_timer_));
        status_timer_ = nullptr;
    }
#endif
}

void DeepDogMotorMqtt::SetMotor(DeepMotor* motor) {
#if DEEP_DOG_MOTOR_ENABLE
    motor_ = motor;
    if (motor_) {
        motor_->setMotorDiscoveryCallback(&DeepDogMotorMqtt::DiscoveryCb, this);
    }
#else
    (void)motor;
#endif
}

void DeepDogMotorMqtt::StatusTimerCb(void* arg) {
    auto* self = static_cast<DeepDogMotorMqtt*>(arg);
    if (self) {
        self->PublishStatus(false);
    }
}

bool DeepDogMotorMqtt::PublishStatus(bool force) {
    (void)force;
#if !DEEP_DOG_MOTOR_ENABLE
    return false;
#else
    if (!enabled_ || !connected_ || !client_) {
        return false;
    }
    DeepMotor* motor = motor_ ? motor_ : DeepDogMotorGet();
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", motor != nullptr);
    cJSON* arr = cJSON_AddArrayToObject(root, "motors");
    if (motor) {
        int8_t ids[MAX_MOTOR_COUNT];
        const uint8_t n = motor->getRegisteredMotorIds(ids, MAX_MOTOR_COUNT);
        for (uint8_t i = 0; i < n; ++i) {
            const uint8_t mid = static_cast<uint8_t>(ids[i]);
            motor_status_t st {};
            if (!motor->getMotorStatus(mid, &st)) {
                continue;
            }
            cJSON* m = BuildMotorStatusJson(mid, st, motor);
            if (!m) {
                continue;
            }
            cJSON_AddItemToArray(arr, m);
        }
    }
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(UnixTs()));
    char* s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!s) {
        return false;
    }
    const bool ok = client_->Publish("motor/status", s, 0, true);
    cJSON_free(s);
    return ok;
#endif
}

void DeepDogMotorMqtt::ApplyCmd(const char* json) {
#if !DEEP_DOG_MOTOR_ENABLE
    (void)json;
#else
    DeepMotor* motor = motor_ ? motor_ : DeepDogMotorGet();
    if (!motor) {
        ESP_LOGW(TAG, "motor/cmd ignored (no DeepMotor)");
        return;
    }
    cJSON* root = cJSON_Parse(json);
    if (!root) {
        return;
    }
    const cJSON* id_j = cJSON_GetObjectItem(root, "motor_id");
    if (!cJSON_IsNumber(id_j)) {
        cJSON_Delete(root);
        return;
    }
    const uint8_t motor_id = static_cast<uint8_t>(id_j->valueint);
    if (motor_id == 0) {
        cJSON_Delete(root);
        return;
    }
    motor->registerMotor(motor_id);

    const cJSON* reset = cJSON_GetObjectItem(root, "reset");
    if (cJSON_IsTrue(reset)) {
        MotorProtocol::resetMotor(motor_id);
        motor->invalidateMotorCommandCache(motor_id);
    }

    const cJSON* enable = cJSON_GetObjectItem(root, "enable");
    if (cJSON_IsBool(enable)) {
        if (cJSON_IsTrue(enable)) {
            (void)motor->initializeMotor(motor_id, 5.0f);
        } else {
            MotorProtocol::resetMotor(motor_id);
            motor->invalidateMotorCommandCache(motor_id);
        }
    }

    const cJSON* speed = cJSON_GetObjectItem(root, "speed_limit");
    if (cJSON_IsNumber(speed)) {
        (void)motor->setMotorSpeedLimit(motor_id, static_cast<float>(speed->valuedouble));
    }

    const cJSON* pos = cJSON_GetObjectItem(root, "position_rad");
    if (cJSON_IsNumber(pos)) {
        const float p = ClampPos(static_cast<float>(pos->valuedouble));
        float lim = 5.0f;
        if (cJSON_IsNumber(speed)) {
            lim = static_cast<float>(speed->valuedouble);
            (void)motor->setMotorPosition(motor_id, p, lim);
        } else {
            (void)motor->setMotorPositionRefOnly(motor_id, p);
        }
    }

    const cJSON* iq = cJSON_GetObjectItem(root, "iq_ref");
    if (cJSON_IsNumber(iq)) {
        (void)motor->setMotorIqRef(motor_id, static_cast<float>(iq->valuedouble));
    }

    const cJSON* mit = cJSON_GetObjectItem(root, "mit");
    if (cJSON_IsObject(mit)) {
        const cJSON* mp = cJSON_GetObjectItem(mit, "position_rad");
        const cJSON* mv = cJSON_GetObjectItem(mit, "velocity_rad_s");
        const cJSON* mkp = cJSON_GetObjectItem(mit, "kp");
        const cJSON* mkd = cJSON_GetObjectItem(mit, "kd");
        const cJSON* mt = cJSON_GetObjectItem(mit, "torque_ff");
        const float p = cJSON_IsNumber(mp) ? ClampPos(static_cast<float>(mp->valuedouble)) : 0.f;
        const float v = cJSON_IsNumber(mv) ? static_cast<float>(mv->valuedouble) : 0.f;
        const float kp = cJSON_IsNumber(mkp) ? static_cast<float>(mkp->valuedouble) : 1.f;
        const float kd = cJSON_IsNumber(mkd) ? static_cast<float>(mkd->valuedouble) : 1.f;
        const float tau = cJSON_IsNumber(mt) ? static_cast<float>(mt->valuedouble) : 0.f;
        (void)motor->setMotorMitCommand(motor_id, p, v, kp, kd, tau);
    }

    const cJSON* set_zero = cJSON_GetObjectItem(root, "set_zero");
    if (cJSON_IsTrue(set_zero)) {
        (void)MotorProtocol::setMotorZero(motor_id);
    }

    const cJSON* teaching = cJSON_GetObjectItem(root, "teaching");
    if (cJSON_IsString(teaching) && teaching->valuestring) {
        if (strcmp(teaching->valuestring, "start") == 0) {
            (void)motor->startTeaching(motor_id);
        } else if (strcmp(teaching->valuestring, "stop") == 0) {
            (void)motor->stopTeaching();
        } else if (strcmp(teaching->valuestring, "play") == 0) {
            (void)motor->executeTeaching(motor_id);
        }
    }

    cJSON_Delete(root);
    PublishStatus(true);
#endif
}

void DeepDogMotorMqtt::DiscoveryCb(uint8_t motor_id, const motor_status_t& status, void* user_data) {
    auto* self = static_cast<DeepDogMotorMqtt*>(user_data);
    if (!self) {
        return;
    }
    self->PublishDiscoveryEvent(motor_id, status);
    self->PublishStatus(true);
}

void DeepDogMotorMqtt::PublishDiscoveryEvent(uint8_t motor_id, const motor_status_t& status) {
#if !DEEP_DOG_MOTOR_ENABLE
    (void)motor_id;
    (void)status;
#else
    if (!enabled_ || !connected_ || !client_) {
        return;
    }
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "event", "discovered");
    cJSON_AddNumberToObject(root, "id", motor_id);
    if (status.has_device_id) {
        char uid_hex[17];
        snprintf(uid_hex, sizeof(uid_hex), "%016llX",
                 (unsigned long long)status.mcu_uid);
        cJSON_AddStringToObject(root, "mcu_uid", uid_hex);
    }
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(UnixTs()));
    char* s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (s) {
        client_->Publish("motor/scan_result", s, 0, false);
        cJSON_free(s);
    }
#endif
}

void DeepDogMotorMqtt::OnConnected() {
#if !DEEP_DOG_MOTOR_ENABLE
    return;
#else
    if (!enabled_ || !client_) {
        return;
    }
    connected_ = true;
    if (!motor_) {
        motor_ = DeepDogMotorGet();
    }
    if (motor_) {
        motor_->setMotorDiscoveryCallback(&DeepDogMotorMqtt::DiscoveryCb, this);
    }
    client_->Subscribe("motor/cmd", 1);
    client_->Subscribe("motor/scan", 1);
    client_->Subscribe("motor/report_start", 1);
    client_->Subscribe("motor/report_stop", 1);
    PublishStatus(true);
    if (status_timer_) {
        esp_timer_start_periodic(static_cast<esp_timer_handle_t>(status_timer_), 500000);
    }
#endif
}

void DeepDogMotorMqtt::OnDisconnected() {
    connected_ = false;
#if DEEP_DOG_MOTOR_ENABLE
    if (status_timer_) {
        esp_timer_stop(static_cast<esp_timer_handle_t>(status_timer_));
    }
#endif
}

void DeepDogMotorMqtt::Stop() {
    OnDisconnected();
}

void DeepDogMotorMqtt::OnMessage(const std::string& topic, const std::string& payload) {
#if !DEEP_DOG_MOTOR_ENABLE
    (void)topic;
    (void)payload;
#else
    if (!enabled_) {
        return;
    }
    if (client_ && topic == client_->Topic("motor/cmd")) {
        ApplyCmd(payload.c_str());
        return;
    }
    if (client_ && topic == client_->Topic("motor/scan")) {
        DeepMotor* motor = motor_ ? motor_ : DeepDogMotorGet();
        if (!motor) {
            return;
        }
        motor->sendBusScanProbes();
        cJSON* root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "started", true);
        cJSON* range = cJSON_AddArrayToObject(root, "range");
        cJSON_AddItemToArray(range, cJSON_CreateNumber(1));
        cJSON_AddItemToArray(range, cJSON_CreateNumber(127));
        cJSON_AddNumberToObject(root, "ts", static_cast<double>(UnixTs()));
        char* s = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (s) {
            client_->Publish("motor/scan_result", s, 0, false);
            cJSON_free(s);
        }
        return;
    }
    if (client_ && topic == client_->Topic("motor/report_start")) {
        DeepMotor* motor = motor_ ? motor_ : DeepDogMotorGet();
        if (!motor) {
            return;
        }
        int8_t ids[MAX_MOTOR_COUNT];
        const uint8_t n = motor->getRegisteredMotorIds(ids, MAX_MOTOR_COUNT);
        for (uint8_t i = 0; i < n; ++i) {
            motor->requestActiveReport(static_cast<uint8_t>(ids[i]));
        }
        report_active_ = true;
        PublishStatus(true);
        return;
    }
    if (client_ && topic == client_->Topic("motor/report_stop")) {
        DeepMotor* motor = motor_ ? motor_ : DeepDogMotorGet();
        if (motor) {
            int8_t ids[MAX_MOTOR_COUNT];
            const uint8_t n = motor->getRegisteredMotorIds(ids, MAX_MOTOR_COUNT);
            for (uint8_t i = 0; i < n; ++i) {
                motor->releaseActiveReport(static_cast<uint8_t>(ids[i]));
            }
        }
        report_active_ = false;
        return;
    }
#endif
}
