#pragma once

#include <esp_timer.h>

#include <string>

class DeepDogMqttClient;

/** face/cmd 订阅 + face/status 上行（像素坐标，on_change + ~2 Hz 轮询） */
class DeepDogFaceMqtt {
public:
    explicit DeepDogFaceMqtt(DeepDogMqttClient* client);
    ~DeepDogFaceMqtt();

    void SetEnabled(bool enabled) { enabled_ = enabled; }

    void OnConnected();
    void OnDisconnected();
    void Stop();

    void OnMessage(const std::string& topic, const std::string& payload);
    bool PublishStatus(bool force = false);

private:
    static void PollTimerCb(void* arg);

    DeepDogMqttClient* client_;
    esp_timer_handle_t poll_timer_ = nullptr;
    bool enabled_ = false;
    std::string last_fingerprint_;
};
