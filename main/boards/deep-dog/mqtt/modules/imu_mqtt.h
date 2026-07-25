#pragma once

#include "sensor/imu_config.h"

#include <esp_timer.h>

class DeepDogMqttClient;
#if DEEP_DOG_IMU_ENABLE
class DeepDogImuSensor;
class DeepDogImuSwitch;
#endif

/** imu/status 上行 ~10 Hz，QoS0，retain=false；含 12 路 switches 边沿计数 */
class DeepDogImuMqtt {
public:
    explicit DeepDogImuMqtt(DeepDogMqttClient* client);
    ~DeepDogImuMqtt();

#if DEEP_DOG_IMU_ENABLE
    void SetSensor(DeepDogImuSensor* sensor) { sensor_ = sensor; }
    void SetSwitchHub(DeepDogImuSwitch* hub) { switch_hub_ = hub; }
#else
    void SetSensor(void*) {}
    void SetSwitchHub(void*) {}
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
    DeepDogImuSwitch* switch_hub_ = nullptr;
#endif
    esp_timer_handle_t timer_ = nullptr;
    bool enabled_ = false;
};
