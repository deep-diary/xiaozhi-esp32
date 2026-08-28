#include "mqtt/modules/motor_mqtt.h"

#include "mqtt/mqtt_client.h"
#include "config.h"
#include "mcp_server.h"

#if DEEP_DOG_MOTOR_ENABLE
#include "motor/deep_motor.h"
#include "motor/protocol_motor.h"
#include "motor/motor_access.h"
#include "motor/motor_config.h"
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

#if DEEP_DOG_MOTOR_ENABLE
constexpr int64_t kStatusThrottleUs =
    static_cast<int64_t>(DEEP_DOG_MOTOR_MQTT_STATUS_THROTTLE_MS) * 1000LL;
constexpr int64_t kStatusHeartbeatUs =
    static_cast<int64_t>(DEEP_DOG_MOTOR_MQTT_STATUS_HEARTBEAT_MS) * 1000LL;
#endif

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

const char* RunModeStr(motor_run_mode_t mode) {
    switch (mode) {
        case MOTOR_CTRL_MODE:
            return "mit";
        case MOTOR_POS_MODE:
            return "position";
        case MOTOR_SPEED_MODE:
            return "speed";
        case MOTOR_CURRENT_MODE:
            return "current";
        default:
            return "unknown";
    }
}

const char* DriveStateStr(motor_mode_t mode) {
    return ModeStatusStr(mode);
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
    cJSON_AddStringToObject(m, "drive_state", DriveStateStr(st.mode_status));
    cJSON_AddStringToObject(m, "mode_status", DriveStateStr(st.mode_status));
    cJSON_AddBoolToObject(m, "motor_enabled", st.motor_enabled);
    if (st.run_mode_known) {
        cJSON_AddStringToObject(m, "run_mode", RunModeStr(st.run_mode));
    }
    cJSON_AddBoolToObject(m, "has_device_id", st.has_device_id);
    if (st.has_device_id) {
        char uid_hex[17];
        MotorProtocol::FormatMcuUidHex(st.mcu_uid, uid_hex, sizeof(uid_hex));
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
    esp_timer_handle_t throttle = nullptr;
    esp_timer_create_args_t throttle_args = {
        .callback = &DeepDogMotorMqtt::ThrottleTimerCb,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "dog_motor_mqtt_th",
        .skip_unhandled_events = true,
    };
    if (esp_timer_create(&throttle_args, &throttle) == ESP_OK) {
        throttle_timer_ = throttle;
    }

    esp_timer_handle_t heartbeat = nullptr;
    esp_timer_create_args_t heartbeat_args = {
        .callback = &DeepDogMotorMqtt::HeartbeatTimerCb,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "dog_motor_mqtt_hb",
        .skip_unhandled_events = true,
    };
    if (esp_timer_create(&heartbeat_args, &heartbeat) == ESP_OK) {
        heartbeat_timer_ = heartbeat;
    }
#endif
}

DeepDogMotorMqtt::~DeepDogMotorMqtt() {
    Stop();
#if DEEP_DOG_MOTOR_ENABLE
    if (throttle_timer_) {
        esp_timer_delete(static_cast<esp_timer_handle_t>(throttle_timer_));
        throttle_timer_ = nullptr;
    }
    if (heartbeat_timer_) {
        esp_timer_delete(static_cast<esp_timer_handle_t>(heartbeat_timer_));
        heartbeat_timer_ = nullptr;
    }
#endif
}

void DeepDogMotorMqtt::SetMotor(DeepMotor* motor) {
#if DEEP_DOG_MOTOR_ENABLE
    if (motor_ && motor_ != motor) {
        motor_->setMotorDiscoveryCallback(nullptr, nullptr);
        motor_->setMotorStatusNotifyCallback(nullptr, nullptr);
    }
    motor_ = motor;
    if (motor_) {
        motor_->setMotorDiscoveryCallback(&DeepDogMotorMqtt::OnMotorDiscovered, this);
        motor_->setMotorStatusNotifyCallback(&DeepDogMotorMqtt::OnMotorStatusUpdated, this);
    }
#else
    (void)motor;
#endif
}

void DeepDogMotorMqtt::PublishScanStarted(uint8_t id_min, uint8_t id_max) {
#if !DEEP_DOG_MOTOR_ENABLE
    (void)id_min;
    (void)id_max;
#else
    if (!enabled_ || !connected_ || !client_) {
        return;
    }
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "started", true);
    cJSON* range = cJSON_AddArrayToObject(root, "range");
    cJSON_AddItemToArray(range, cJSON_CreateNumber(id_min));
    cJSON_AddItemToArray(range, cJSON_CreateNumber(id_max));
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(UnixTs()));
    char* s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (s) {
        (void)client_->Publish("motor/scan_result", s, 0, false);
        cJSON_free(s);
    }
#endif
}

#if DEEP_DOG_MOTOR_ENABLE
void DeepDogMotorMqtt::PublishScanDiscovered(uint8_t motor_id, const motor_status_t& status) {
    if (!enabled_ || !connected_ || !client_) {
        return;
    }
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "event", "discovered");
    cJSON_AddNumberToObject(root, "id", motor_id);
    if (status.has_device_id) {
        char uid_hex[17];
        MotorProtocol::FormatMcuUidHex(status.mcu_uid, uid_hex, sizeof(uid_hex));
        cJSON_AddStringToObject(root, "mcu_uid", uid_hex);
        cJSON_AddStringToObject(root, "mcu_uid_hex", uid_hex);
    }
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(UnixTs()));
    char* s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (s) {
        (void)client_->Publish("motor/scan_result", s, 0, false);
        cJSON_free(s);
    }
}

void DeepDogMotorMqtt::OnMotorDiscovered(uint8_t motor_id, const motor_status_t& status, void* user_data) {
    auto* self = static_cast<DeepDogMotorMqtt*>(user_data);
    (void)MotorProtocol::requestSoftwareVersion(motor_id);
    if (status.has_device_id) {
        char uid_hex[17];
        MotorProtocol::FormatMcuUidHex(status.mcu_uid, uid_hex, sizeof(uid_hex));
        ESP_LOGI(TAG, "扫描发现电机 %u mcu_uid=%s", (unsigned)motor_id, uid_hex);
    } else {
        ESP_LOGI(TAG, "扫描发现电机 %u", (unsigned)motor_id);
    }
    if (self) {
        self->PublishScanDiscovered(motor_id, status);
        self->PublishStatus(true);
    }
}
#endif

void DeepDogMotorMqtt::OnMotorStatusUpdated(uint8_t motor_id, void* user_data) {
    (void)motor_id;
#if DEEP_DOG_MOTOR_ENABLE
    auto* self = static_cast<DeepDogMotorMqtt*>(user_data);
    if (self) {
        self->ScheduleStatusPublish();
    }
#else
    (void)user_data;
#endif
}

void DeepDogMotorMqtt::ScheduleStatusPublish() {
#if !DEEP_DOG_MOTOR_ENABLE
    return;
#else
    if (!enabled_ || !connected_) {
        return;
    }
    const int64_t now = esp_timer_get_time();
    if (last_publish_us_ == 0 || now - last_publish_us_ >= kStatusThrottleUs) {
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
#endif
}

void DeepDogMotorMqtt::ThrottleTimerCb(void* arg) {
    auto* self = static_cast<DeepDogMotorMqtt*>(arg);
    if (!self) {
        return;
    }
    if (self->pending_publish_) {
        self->pending_publish_ = false;
        self->PublishStatus(true);
    }
}

void DeepDogMotorMqtt::HeartbeatTimerCb(void* arg) {
    auto* self = static_cast<DeepDogMotorMqtt*>(arg);
    if (self) {
        self->PublishStatus(true);
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
    mcp.AppendToolsToJsonArray(tools, "self.motor.");  // MOT-14：电机工具统一 self.motor.*
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
    // MOT-14：粘性默认电机（-1 = 无活跃电机）
    cJSON_AddNumberToObject(root, "active_id", motor ? motor->getActiveMotorId() : -1);
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
            cJSON_AddNumberToObject(m, "can_id", mid);
            cJSON_AddStringToObject(m, "init_state", InitStateStr(motor->getMotorInitState(mid)));
            AppendMotorStatusFields(m, st);
            cJSON_AddBoolToObject(m, "teaching_recording", motor->isMotorRecording(mid));
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
    if (ok) {
        last_publish_us_ = esp_timer_get_time();
        pending_publish_ = false;
    }
    return ok;
#endif
}

bool DeepDogMotorMqtt::PublishTeachingSnapshot(uint8_t motor_id) {
#if !DEEP_DOG_MOTOR_ENABLE
    (void)motor_id;
    return false;
#else
    if (!enabled_ || !connected_ || !client_) {
        return false;
    }
    DeepMotor* motor = motor_ ? motor_ : DeepDogMotorGet();
    if (!motor) {
        return false;
    }
    char* json = motor->buildTeachingSnapshotJson(motor_id);
    if (!json) {
        return false;
    }
    const bool ok = client_->Publish("motor/teaching/snapshot", json, 0, false);
    cJSON_free(json);
    if (ok) {
        ESP_LOGI(TAG, "motor/teaching/snapshot motor_id=%u", (unsigned)motor_id);
    }
    return ok;
#endif
}

bool DeepDogMotorMqtt::PublishTeachingStatus() {
#if !DEEP_DOG_MOTOR_ENABLE
    return false;
#else
    if (!enabled_ || !connected_ || !client_) {
        return false;
    }
    DeepMotor* motor = motor_ ? motor_ : DeepDogMotorGet();
    if (!motor) {
        return false;
    }
    char* json = motor->buildTeachingStatusJson();
    if (!json) {
        return false;
    }
    const bool ok = client_->Publish("motor/teaching/status", json, 0, true);
    cJSON_free(json);
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
            if (tool_name && strncmp(tool_name, "self.motor.", 11) == 0) {
                auto result = McpServer::GetInstance().InvokeToolSync(tool_name, tool_args);
                PublishMcpResult(tool_name, result.ok, result.result_json.c_str(),
                                 result.error_message.c_str());
                ESP_LOGI(TAG, "mcp_call %s ok=%d", tool_name, result.ok ? 1 : 0);
            } else {
                PublishMcpResult(tool_name ? tool_name : "", false, nullptr, "tool not allowed (motor only)");
            }
        }
        cJSON_Delete(root);
        ScheduleStatusPublish();
        return;
    }

    // MOT-14 粘性默认电机：motor_id 缺省/0 = 当前活跃电机；显式 >0 注册后置为活跃
    const cJSON* id_j = cJSON_GetObjectItem(root, "motor_id");
    uint8_t motor_id = 0;
    if (cJSON_IsNumber(id_j) && id_j->valueint > 0) {
        motor_id = static_cast<uint8_t>(id_j->valueint);
        EnsureMotorRegistered(motor, motor_id);
        (void)motor->setActiveMotorId(motor_id);
    } else {
        const int8_t active = motor->getActiveMotorId();
        if (active <= 0) {
            ESP_LOGW(TAG, "motor/cmd 无 motor_id 且当前无活跃电机，忽略");
            cJSON_Delete(root);
            return;
        }
        motor_id = static_cast<uint8_t>(active);
    }

    const cJSON* reset = cJSON_GetObjectItem(root, "reset");
    if (cJSON_IsTrue(reset)) {
        MotorProtocol::resetMotor(motor_id);
        motor->invalidateMotorCommandCache(motor_id);
        motor->markMotorEnabled(motor_id, false);
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
            motor->markMotorEnabled(motor_id, false);
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

    const cJSON* speed_ref_j = cJSON_GetObjectItem(root, "speed_rad_s");
    const cJSON* pos_j = cJSON_GetObjectItem(root, "position_rad");
    const cJSON* speed = cJSON_GetObjectItem(root, "speed_limit");
    if (cJSON_IsNumber(speed_ref_j)) {
        const float spd = static_cast<float>(speed_ref_j->valuedouble);
        const bool sent = motor->setMotorSpeedRef(motor_id, spd);
        ESP_LOGI(TAG, "motor/cmd speed_rad_s motor_id=%u spd=%.3f sent=%d", (unsigned)motor_id, (double)spd,
                 sent ? 1 : 0);
    } else if (cJSON_IsNumber(speed) && !cJSON_IsNumber(pos_j)) {
        const float spd = static_cast<float>(speed->valuedouble);
        const bool sent = motor->setMotorSpeedRef(motor_id, spd);
        ESP_LOGI(TAG, "motor/cmd speed_limit→spd_ref motor_id=%u spd=%.3f sent=%d", (unsigned)motor_id,
                 (double)spd, sent ? 1 : 0);
    } else if (cJSON_IsNumber(speed) && cJSON_IsNumber(pos_j)) {
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

    const cJSON* teaching = cJSON_GetObjectItem(root, "teaching");
    if (cJSON_IsString(teaching) && teaching->valuestring) {
        const char* action = teaching->valuestring;
        const cJSON* ids_j = cJSON_GetObjectItem(root, "teaching_motor_ids");
        uint8_t multi_ids[MAX_MOTOR_COUNT];
        uint8_t multi_count = 0;
        if (cJSON_IsArray(ids_j)) {
            const int n = cJSON_GetArraySize(ids_j);
            for (int i = 0; i < n && multi_count < MAX_MOTOR_COUNT; ++i) {
                const cJSON* item = cJSON_GetArrayItem(ids_j, i);
                if (cJSON_IsNumber(item) && item->valueint > 0) {
                    multi_ids[multi_count++] = static_cast<uint8_t>(item->valueint);
                    EnsureMotorRegistered(motor, multi_ids[multi_count - 1]);
                }
            }
        }
        if (strcmp(action, "start") == 0) {
            TeachingRecordConfig rc = TeachingRecordConfigDefault();
            const cJSON* sp = cJSON_GetObjectItem(root, "sample_period_ms");
            if (cJSON_IsNumber(sp) && sp->valueint > 0) {
                rc.sample_period_ms = static_cast<uint32_t>(sp->valueint);
            }
            if (multi_count > 0) {
                (void)motor->startTeachingMulti(multi_ids, multi_count, &rc);
                ESP_LOGI(TAG, "motor/cmd teaching start multi count=%u", (unsigned)multi_count);
            } else {
                (void)motor->startTeaching(motor_id, &rc);
                ESP_LOGI(TAG, "motor/cmd teaching start motor_id=%u", (unsigned)motor_id);
            }
            (void)PublishTeachingStatus();
        } else if (strcmp(action, "stop") == 0) {
            (void)motor->stopTeaching();
            ESP_LOGI(TAG, "motor/cmd teaching stop");
            int8_t ids[MAX_MOTOR_COUNT];
            const uint8_t n = motor->getRegisteredMotorIds(ids, MAX_MOTOR_COUNT);
            bool published = false;
            for (uint8_t i = 0; i < n; ++i) {
                const uint8_t mid = static_cast<uint8_t>(ids[i]);
                if (motor->getTeachingPointCount(mid) > 0) {
                    (void)PublishTeachingSnapshot(mid);
                    published = true;
                }
            }
            if (!published) {
                (void)PublishTeachingSnapshot(motor_id);
            }
            (void)PublishTeachingStatus();
        } else if (strcmp(action, "play") == 0) {
            TeachingPlayConfig play_cfg = TeachingPlayConfigDefault();
            const cJSON* dur = cJSON_GetObjectItem(root, "play_duration_ms");
            const cJSON* blend = cJSON_GetObjectItem(root, "play_blend_ms");
            const cJSON* pkp = cJSON_GetObjectItem(root, "play_kp");
            const cJSON* pkd = cJSON_GetObjectItem(root, "play_kd");
            const cJSON* ptau = cJSON_GetObjectItem(root, "play_tau_ff");
            const cJSON* pts = cJSON_GetObjectItem(root, "play_time_scale");
            const cJSON* put = cJSON_GetObjectItem(root, "play_use_timeline");
            if (cJSON_IsNumber(dur)) {
                play_cfg.duration_ms = static_cast<uint32_t>(dur->valuedouble);
            }
            if (cJSON_IsNumber(blend)) {
                play_cfg.blend_ms = static_cast<uint32_t>(blend->valuedouble);
            }
            if (cJSON_IsNumber(pkp)) {
                play_cfg.kp = static_cast<float>(pkp->valuedouble);
            }
            if (cJSON_IsNumber(pkd)) {
                play_cfg.kd = static_cast<float>(pkd->valuedouble);
            }
            if (cJSON_IsNumber(ptau)) {
                play_cfg.tau_ff = static_cast<float>(ptau->valuedouble);
            }
            if (cJSON_IsNumber(pts)) {
                play_cfg.time_scale = static_cast<float>(pts->valuedouble);
            }
            if (cJSON_IsBool(put)) {
                play_cfg.use_recorded_timeline = cJSON_IsTrue(put);
            }
            bool ok = false;
            if (multi_count > 0) {
                ok = motor->executeTeachingMulti(multi_ids, multi_count, &play_cfg);
            } else {
                ok = motor->executeTeaching(motor_id, &play_cfg);
            }
            ESP_LOGI(TAG, "motor/cmd teaching play ok=%d time_scale=%.2f", ok ? 1 : 0,
                     (double)play_cfg.time_scale);
        }
    }

    cJSON_Delete(root);
    ScheduleStatusPublish();
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
        motor_->setMotorDiscoveryCallback(&DeepDogMotorMqtt::OnMotorDiscovered, this);
        motor_->setMotorStatusNotifyCallback(&DeepDogMotorMqtt::OnMotorStatusUpdated, this);
    }
    client_->Subscribe("motor/cmd", 1);
    client_->Subscribe("motor/scan", 0);
    PublishTools();
    PublishStatus(true);
    if (heartbeat_timer_) {
        esp_timer_start_periodic(static_cast<esp_timer_handle_t>(heartbeat_timer_), kStatusHeartbeatUs);
    }
#endif
}

void DeepDogMotorMqtt::OnDisconnected() {
    connected_ = false;
#if DEEP_DOG_MOTOR_ENABLE
    pending_publish_ = false;
    if (throttle_timer_) {
        esp_timer_stop(static_cast<esp_timer_handle_t>(throttle_timer_));
    }
    if (heartbeat_timer_) {
        esp_timer_stop(static_cast<esp_timer_handle_t>(heartbeat_timer_));
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
        if (motor) {
            PublishScanStarted(1, 127);
            motor->sendBusScanProbes();
            ESP_LOGI(TAG, "motor/scan -> probes 1-127");
        }
    }
#endif
}
