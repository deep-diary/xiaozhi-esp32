#pragma once

#include <esp_timer.h>

#include <string>

class DeepDogMqttClient;

/** track/cmd + track/status；消费 face snapshot，actuator=none（v0.1） */
class DeepDogTrackMqtt {
public:
    explicit DeepDogTrackMqtt(DeepDogMqttClient* client);
    ~DeepDogTrackMqtt();

    void SetModuleEnabled(bool enabled) { module_enabled_ = enabled; }

    void OnConnected();
    void OnDisconnected();
    void Stop();

    void OnMessage(const std::string& topic, const std::string& payload);
    bool PublishStatus(bool force = false);

private:
    static void PollTimerCb(void* arg);

    DeepDogMqttClient* client_;
    esp_timer_handle_t poll_timer_ = nullptr;
    bool module_enabled_ = false;
    /** 用户是否打开跟踪（track/cmd） */
    bool user_tracking_ = false;
    std::string last_fingerprint_;
};
