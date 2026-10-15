#pragma once

#include <esp_timer.h>

#include <string>

class DeepDogMqttClient;

/**
 * deep-trace 遥测上行（见 swrs/mqtt/modules/15-telemetry.md）。
 * 话题为云端按工位分配的 deep-trace/{home}/{wu}/telemetry，**不挂**模块前缀。
 * 未配置 telemetry_topic 时自禁用，不发。
 */
class DeepDogTelemetryMqtt {
public:
    explicit DeepDogTelemetryMqtt(DeepDogMqttClient* client);
    ~DeepDogTelemetryMqtt();

    void OnConnected();
    void OnDisconnected();
    void Stop();

    /** 从 NVS 重载 telemetry_topic / home_code / work_unit_code / device_program */
    void ReloadConfig();
    bool enabled() const { return enabled_; }

private:
    static void TimerCb(void* arg);
    void Publish();

    DeepDogMqttClient* client_;
    esp_timer_handle_t timer_ = nullptr;
    bool enabled_ = false;
    std::string topic_;
    std::string home_code_;
    std::string work_unit_code_;
    std::string device_program_;
};
