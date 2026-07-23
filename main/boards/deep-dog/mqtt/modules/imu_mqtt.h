#pragma once

#include "sensor/imu_config.h"

#include <esp_timer.h>

class DeepDogMqttClient;
#if DEEP_DOG_IMU_ENABLE
class DeepDogImuSensor;
#endif

/** imu/status 上行 ~10 Hz，QoS0，retain=false */
class DeepDogImuMqtt {
public:
    explicit DeepDogImuMqtt(DeepDogMqttClient* client);
    ~DeepDogImuMqtt();

#if DEEP_DOG_IMU_ENABLE
    void SetSensor(DeepDogImuSensor* sensor) { sensor_ = sensor; }
#else
    void SetSensor(void*) {}
#endif
    void SetEnabled(bool enabled) { enabled_ = enabled; }

    void OnConnected();
    void OnDisconnected();
    void Stop();

    bool PublishStatus();

private:
    static void TimerCb(void* arg);

    DeepDogMqttClient* client_;
#if DEEP_DOG_IMU_ENABLE
    DeepDogImuSensor* sensor_ = nullptr;
#endif
    esp_timer_handle_t timer_ = nullptr;
    bool enabled_ = false;
};
