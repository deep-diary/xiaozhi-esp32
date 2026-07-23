#include "mqtt/modules/imu_mqtt.h"

#include "mqtt/mqtt_client.h"
#include "mqtt/mqtt_config.h"
#include "sensor/imu_config.h"
#include "sensor/imu_sensor.h"

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
    // 常见加速度计姿态估计（deg）
    constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;
    if (pitch_deg) {
        *pitch_deg = std::atan2(-ax, std::sqrt(ay * ay + az * az)) * kRadToDeg;
    }
    if (roll_deg) {
        *roll_deg = std::atan2(ay, az) * kRadToDeg;
    }
}

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
    if (sensor_ && sensor_->IsInitialized()) {
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
#endif

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
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(UnixTs()));

    char* printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return false;
    }
    const bool pub_ok = client_->Publish("imu/status", printed, 0, false);
    static int s_log = 0;
    if (s_log < 3 || (s_log % 50) == 0) {
        ESP_LOGI(TAG, "imu/status ok=%d accel_g=%.2f pitch=%.1f roll=%.1f pub=%d", ok ? 1 : 0, accel_g, pitch, roll,
                 pub_ok ? 1 : 0);
    }
    ++s_log;
    cJSON_free(printed);
    return pub_ok;
}
