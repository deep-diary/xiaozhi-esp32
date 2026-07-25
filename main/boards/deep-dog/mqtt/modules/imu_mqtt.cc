#include "mqtt/modules/imu_mqtt.h"

#include "mqtt/mqtt_client.h"
#include "mqtt/mqtt_config.h"
#include "sensor/imu_config.h"
#if DEEP_DOG_IMU_ENABLE
#include "sensor/imu_sensor.h"
#include "sensor/imu_switch.h"
#endif

#include <cJSON.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <cmath>
#include <ctime>

#define TAG "dog_mqtt_imu"

namespace {

int64_t UnixTs() {
    const time_t now = time(nullptr);
    if (now > 1000000000) {
        return static_cast<int64_t>(now);
    }
    return static_cast<int64_t>(esp_timer_get_time() / 1000000LL);
}

void AccelToPitchRoll(float ax, float ay, float az, float* pitch_deg, float* roll_deg) {
    constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;
    if (pitch_deg) {
        *pitch_deg = std::atan2(-ax, std::sqrt(ay * ay + az * az)) * kRadToDeg;
    }
    if (roll_deg) {
        *roll_deg = std::atan2(ay, az) * kRadToDeg;
    }
}

#if DEEP_DOG_IMU_ENABLE
const char* kSwitchJsonKeys[kDeepDogImuSwitchCount] = {
    "rot_x_pos",   "rot_x_neg",   "rot_y_pos",   "rot_y_neg",   "rot_z_pos",   "rot_z_neg",
    "trans_x_pos", "trans_x_neg", "trans_y_pos", "trans_y_neg", "trans_z_pos", "trans_z_neg",
};
#endif

}  // namespace

DeepDogImuMqtt::DeepDogImuMqtt(DeepDogMqttClient* client) : client_(client) {
    esp_timer_create_args_t args = {
        .callback = &DeepDogImuMqtt::TimerCb,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "dog_imu_pub",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&args, &timer_);
}

DeepDogImuMqtt::~DeepDogImuMqtt() {
    Stop();
    if (timer_) {
        esp_timer_delete(timer_);
        timer_ = nullptr;
    }
}

void DeepDogImuMqtt::TimerCb(void* arg) {
    auto* self = static_cast<DeepDogImuMqtt*>(arg);
    if (self) {
        self->PublishStatus();
    }
}

void DeepDogImuMqtt::OnConnected() {
    if (!enabled_) {
        return;
    }
    PublishStatus();
    if (timer_) {
        esp_timer_stop(timer_);
        esp_timer_start_periodic(timer_, DEEP_DOG_MQTT_IMU_INTERVAL_US);
    }
}

void DeepDogImuMqtt::OnDisconnected() {
    if (timer_) {
        esp_timer_stop(timer_);
    }
}

void DeepDogImuMqtt::Stop() {
    OnDisconnected();
}

bool DeepDogImuMqtt::PublishStatus() {
    if (!enabled_ || !client_ || !client_->IsConnected()) {
        return false;
    }

    bool ok = false;
    float ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0;
#if DEEP_DOG_IMU_ENABLE
    int switch_counts[kDeepDogImuSwitchCount] = {};
    if (switch_hub_) {
        DeepDogImuRawData raw{};
        switch_hub_->GetLatest(&raw, &ok);
        if (ok) {
            ax = raw.accel_x;
            ay = raw.accel_y;
            az = raw.accel_z;
            gx = raw.gyro_x;
            gy = raw.gyro_y;
            gz = raw.gyro_z;
        }
        switch_hub_->TakeSwitchCounts(switch_counts);
        if (!ok) {
            for (int i = 0; i < kDeepDogImuSwitchCount; ++i) {
                switch_counts[i] = 0;
            }
        }
    } else if (sensor_ && sensor_->IsInitialized()) {
        DeepDogImuRawData raw{};
        if (sensor_->ReadRawData(&raw)) {
            ok = true;
            ax = raw.accel_x;
            ay = raw.accel_y;
            az = raw.accel_z;
            gx = raw.gyro_x;
            gy = raw.gyro_y;
            gz = raw.gyro_z;
        }
    }

    const float accel_g = std::sqrt(ax * ax + ay * ay + az * az);
    float pitch = 0, roll = 0;
    if (ok) {
        AccelToPitchRoll(ax, ay, az, &pitch, &roll);
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", ok);
    cJSON_AddNumberToObject(root, "accel_x", ax);
    cJSON_AddNumberToObject(root, "accel_y", ay);
    cJSON_AddNumberToObject(root, "accel_z", az);
    cJSON_AddNumberToObject(root, "accel_g", accel_g);
    cJSON_AddNumberToObject(root, "gyro_x", gx);
    cJSON_AddNumberToObject(root, "gyro_y", gy);
    cJSON_AddNumberToObject(root, "gyro_z", gz);
    cJSON_AddNumberToObject(root, "pitch", pitch);
    cJSON_AddNumberToObject(root, "roll", roll);

    cJSON* switches = cJSON_CreateObject();
    for (int i = 0; i < kDeepDogImuSwitchCount; ++i) {
        cJSON_AddNumberToObject(switches, kSwitchJsonKeys[i], switch_counts[i]);
    }
    cJSON_AddItemToObject(root, "switches", switches);
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(UnixTs()));

    char* printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return false;
    }
    const bool pub_ok = client_->Publish("imu/status", printed, 0, false);
    static int s_log = 0;
    int switch_sum = 0;
    for (int i = 0; i < kDeepDogImuSwitchCount; ++i) {
        switch_sum += switch_counts[i];
    }
    if (switch_sum > 0) {
        ESP_LOGI(TAG, "switches rot=[%d,%d,%d,%d,%d,%d] trans=[%d,%d,%d,%d,%d,%d]", switch_counts[0],
                 switch_counts[1], switch_counts[2], switch_counts[3], switch_counts[4], switch_counts[5],
                 switch_counts[6], switch_counts[7], switch_counts[8], switch_counts[9], switch_counts[10],
                 switch_counts[11]);
    }
    if (s_log < 2 || (s_log % 200) == 0) {
        ESP_LOGI(TAG, "imu/status ok=%d accel_g=%.2f pub=%d", ok ? 1 : 0, accel_g, pub_ok ? 1 : 0);
    }
    ++s_log;
    cJSON_free(printed);
    return pub_ok;
#else
    const float accel_g = std::sqrt(ax * ax + ay * ay + az * az);
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", false);
    cJSON_AddNumberToObject(root, "accel_x", 0);
    cJSON_AddNumberToObject(root, "accel_y", 0);
    cJSON_AddNumberToObject(root, "accel_z", 0);
    cJSON_AddNumberToObject(root, "accel_g", accel_g);
    cJSON_AddNumberToObject(root, "gyro_x", 0);
    cJSON_AddNumberToObject(root, "gyro_y", 0);
    cJSON_AddNumberToObject(root, "gyro_z", 0);
    cJSON_AddNumberToObject(root, "pitch", 0);
    cJSON_AddNumberToObject(root, "roll", 0);
    cJSON* switches = cJSON_CreateObject();
    static const char* kKeys[] = {
        "rot_x_pos",   "rot_x_neg",   "rot_y_pos",   "rot_y_neg",   "rot_z_pos",   "rot_z_neg",
        "trans_x_pos", "trans_x_neg", "trans_y_pos", "trans_y_neg", "trans_z_pos", "trans_z_neg",
    };
    for (const char* k : kKeys) {
        cJSON_AddNumberToObject(switches, k, 0);
    }
    cJSON_AddItemToObject(root, "switches", switches);
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(UnixTs()));
    char* printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return false;
    }
    const bool pub_ok = client_->Publish("imu/status", printed, 0, false);
    cJSON_free(printed);
    return pub_ok;
#endif
}
