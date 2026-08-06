#include "mqtt/modules/motor_mqtt.h"

#include "mqtt/mqtt_client.h"
#include "config.h"
#include "mcp_server.h"

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
#include <ctime>
#include <cstring>

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
const char* InitStateStr(MotorInitState st) {
    switch (st) {
        case MotorInitState::Initializing:
            return "initializing";
        case MotorInitState::Ready:
            return "ready";
        case MotorInitState::Failed:
            return "failed";
        case MotorInitState::None:
        default:
            return "none";
    }
}

const char* ModeStatusStr(motor_mode_t mode) {
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

void AppendMotorStatusFields(cJSON* m, const motor_status_t& st) {
    cJSON_AddNumberToObject(m, "position_rad", st.current_angle);
    cJSON_AddNumberToObject(m, "speed_rad_s", st.current_speed);
    cJSON_AddNumberToObject(m, "torque_nm", st.current_torque);
    cJSON_AddNumberToObject(m, "temperature", st.current_temp);
    cJSON_AddNumberToObject(m, "max_abs_torque", st.max_abs_torque);
    cJSON_AddBoolToObject(m, "fault", st.error_status != 0);
    cJSON_AddNumberToObject(m, "error_status", st.error_status);
    cJSON_AddBoolToObject(m, "hall_error", st.hall_error != 0);
    cJSON_AddBoolToObject(m, "magnet_error", st.magnet_error != 0);
    cJSON_AddBoolToObject(m, "temp_error", st.temp_error != 0);
    cJSON_AddBoolToObject(m, "current_error", st.current_error != 0);
    cJSON_AddBoolToObject(m, "voltage_error", st.voltage_error != 0);
    cJSON_AddBoolToObject(m, "collision", st.collision);
    cJSON_AddBoolToObject(m, "has_feedback", st.has_feedback);
    cJSON_AddNumberToObject(m, "feedback_seq", static_cast<double>(st.feedback_seq));
    cJSON_AddNumberToObject(m, "master_id", st.master_id);
    cJSON_AddStringToObject(m, "mode_status", ModeStatusStr(st.mode_status));
    cJSON_AddBoolToObject(m, "has_device_id", st.has_device_id);
    if (st.has_device_id) {
        char uid_hex[17];
        snprintf(uid_hex, sizeof(uid_hex), "%016llX", static_cast<unsigned long long>(st.mcu_uid));
        cJSON_AddStringToObject(m, "mcu_uid_hex", uid_hex);
        cJSON_AddStringToObject(m, "mcu_uid", uid_hex);
    }
    if (st.version[0] != '\0') {
        cJSON_AddStringToObject(m, "version", st.version);
    }
}

void EnsureMotorRegistered(DeepMotor* motor, uint8_t motor_id) {
    if (!motor->isMotorRegistered(motor_id)) {
        (void)motor->registerMotor(motor_id);
    }
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
    if (motor_ && motor_ != motor) {
        motor_->setMotorDiscoveryCallback(nullptr, nullptr);
    }
    motor_ = motor;
    if (motor_) {
        motor_->setMotorDiscoveryCallback(&DeepDogMotorMqtt::OnMotorDiscovered, this);
    }
#else
    (void)motor;
#endif
}

void DeepDogMotorMqtt::OnMotorDiscovered(uint8_t motor_id, const motor_status_t& status, void* user_data) {
    (void)status;
#if DEEP_DOG_MOTOR_ENABLE
    auto* self = static_cast<DeepDogMotorMqtt*>(user_data);
    (void)MotorProtocol::requestSoftwareVersion(motor_id);
    ESP_LOGI(TAG, "扫描发现电机 %u，已请求软件版本", (unsigned)motor_id);
    if (self) {
        self->PublishStatus(true);
    }
#else
    (void)motor_id;
    (void)user_data;
#endif
}

void DeepDogMotorMqtt::StatusTimerCb(void* arg) {
    auto* self = static_cast<DeepDogMotorMqtt*>(arg);
    if (self) {
        self->PublishStatus(false);
    }
}

bool DeepDogMotorMqtt::PublishTools() {
#if !DEEP_DOG_MOTOR_ENABLE
    return false;
#else
    if (!enabled_ || !connected_ || !client_) {
        return false;
    }
    cJSON* root = cJSON_CreateObject();
    cJSON* tools = cJSON_AddArrayToObject(root, "tools");
    auto& mcp = McpServer::GetInstance();
    mcp.AppendToolsToJsonArray(tools, "self.motor.");
    mcp.AppendToolsToJsonArray(tools, "self.can.");
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(UnixTs()));
    char* s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!s) {
        return false;
    }
    const bool ok = client_->Publish("motor/tools", s, 0, true);
    cJSON_free(s);
    return ok;
#endif
}

void DeepDogMotorMqtt::PublishMcpResult(const char* tool_name, bool ok, const char* result_json, const char* error) {
#if !DEEP_DOG_MOTOR_ENABLE
    (void)tool_name;
    (void)ok;
    (void)result_json;
    (void)error;
#else
    if (!client_ || !connected_) {
        return;
    }
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", tool_name ? tool_name : "");
    cJSON_AddBoolToObject(root, "ok", ok);
    if (ok && result_json && result_json[0]) {
        cJSON* parsed = cJSON_Parse(result_json);
        if (parsed) {
            cJSON_AddItemToObject(root, "result", parsed);
        } else {
            cJSON_AddStringToObject(root, "text", result_json);
        }
    }
    if (!ok && error && error[0]) {
        cJSON_AddStringToObject(root, "error", error);
    }
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(UnixTs()));
    char* s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (s) {
        (void)client_->Publish("motor/mcp_result", s, 0, false);
        cJSON_free(s);
    }
#endif
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
            cJSON* m = cJSON_CreateObject();
            cJSON_AddNumberToObject(m, "id", mid);
            cJSON_AddStringToObject(m, "init_state", InitStateStr(motor->getMotorInitState(mid)));
            AppendMotorStatusFields(m, st);
            float target = 0.f;
            if (motor->getMotorTargetAngle(mid, &target)) {
                cJSON_AddNumberToObject(m, "target_rad", target);
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

    const cJSON* mcp_call = cJSON_GetObjectItem(root, "mcp_call");
    if (cJSON_IsObject(mcp_call)) {
        const cJSON* name_j = cJSON_GetObjectItem(mcp_call, "name");
        const cJSON* args_j = cJSON_GetObjectItem(mcp_call, "arguments");
        if (cJSON_IsString(name_j)) {
            const char* tool_name = name_j->valuestring;
            const cJSON* tool_args = cJSON_IsObject(args_j) ? args_j : nullptr;
            if (tool_name && (strncmp(tool_name, "self.motor.", 11) == 0 ||
                              strncmp(tool_name, "self.can.", 9) == 0)) {
                auto result = McpServer::GetInstance().InvokeToolSync(tool_name, tool_args);
                PublishMcpResult(tool_name, result.ok, result.result_json.c_str(),
                                 result.error_message.c_str());
                ESP_LOGI(TAG, "mcp_call %s ok=%d", tool_name, result.ok ? 1 : 0);
            } else {
                PublishMcpResult(tool_name ? tool_name : "", false, nullptr, "tool not allowed (motor/can only)");
            }
        }
        cJSON_Delete(root);
        PublishStatus(true);
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
    EnsureMotorRegistered(motor, motor_id);

    const cJSON* reset = cJSON_GetObjectItem(root, "reset");
    if (cJSON_IsTrue(reset)) {
        MotorProtocol::resetMotor(motor_id);
        motor->invalidateMotorCommandCache(motor_id);
        motor->resetMotorInitState(motor_id);
    }

    const cJSON* enable = cJSON_GetObjectItem(root, "enable");
    if (cJSON_IsBool(enable)) {
        if (cJSON_IsTrue(enable)) {
            (void)motor->initializeMotor(motor_id, 5.0f);
            ESP_LOGI(TAG, "motor/cmd enable motor_id=%u init_state=%s", (unsigned)motor_id,
                     InitStateStr(motor->getMotorInitState(motor_id)));
        } else {
            MotorProtocol::resetMotor(motor_id);
            motor->invalidateMotorCommandCache(motor_id);
            motor->resetMotorInitState(motor_id);
        }
    }

    const MotorInitState init_st = motor->getMotorInitState(motor_id);
    if (init_st == MotorInitState::Initializing) {
        const cJSON* pos_check = cJSON_GetObjectItem(root, "position_rad");
        const cJSON* iq_check = cJSON_GetObjectItem(root, "iq_ref");
        const cJSON* mit_check = cJSON_GetObjectItem(root, "mit");
        if (cJSON_IsNumber(pos_check) || cJSON_IsNumber(iq_check) || cJSON_IsObject(mit_check)) {
            ESP_LOGW(TAG, "motor_id=%u 仍在 initializing，位置/电流指令已发送但建议等 init_state=ready",
                     (unsigned)motor_id);
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
        bool sent = false;
        if (cJSON_IsNumber(speed)) {
            lim = static_cast<float>(speed->valuedouble);
            sent = motor->setMotorPosition(motor_id, p, lim);
        } else {
            sent = motor->setMotorPositionRefOnly(motor_id, p);
        }
        ESP_LOGI(TAG, "motor/cmd position motor_id=%u pos=%.3f rad sent=%d init_state=%s", (unsigned)motor_id,
                 (double)p, sent ? 1 : 0, InitStateStr(motor->getMotorInitState(motor_id)));
    }

    const cJSON* iq = cJSON_GetObjectItem(root, "iq_ref");
    if (cJSON_IsNumber(iq)) {
        const float iq_v = static_cast<float>(iq->valuedouble);
        const bool sent = motor->setMotorIqRef(motor_id, iq_v);
        ESP_LOGI(TAG, "motor/cmd iq_ref motor_id=%u iq=%.3f sent=%d", (unsigned)motor_id, (double)iq_v,
                 sent ? 1 : 0);
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
        const bool sent = motor->setMotorMitCommand(motor_id, p, v, kp, kd, tau);
        ESP_LOGI(TAG, "motor/cmd mit motor_id=%u pos=%.3f sent=%d", (unsigned)motor_id, (double)p, sent ? 1 : 0);
    }

    cJSON_Delete(root);
    PublishStatus(true);
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
    client_->Subscribe("motor/cmd", 1);
    PublishTools();
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
    }
#endif
}
