#pragma once

#include <esp_timer.h>

class DeepDogMqttClient;

struct DeepDogCapabilities {
    bool dog = true;
    bool stream = false;
    bool face = false;
    bool imu = false;
    bool led = false;
    bool servo = false;
    bool gimbal = false;
    bool handle = false;
    bool touch = true;
    bool can = true;
};

/** device/info（retain）+ device/status 心跳 */
class DeepDogDeviceMqtt {
public:
    explicit DeepDogDeviceMqtt(DeepDogMqttClient* client);
    ~DeepDogDeviceMqtt();

    void SetCapabilities(const DeepDogCapabilities& caps) { caps_ = caps; }
    void SetHttpPort(int port) { http_port_ = port; }
    const DeepDogCapabilities& capabilities() const { return caps_; }

    void OnConnected();
    void OnDisconnected();
    void Stop();

    bool PublishInfo();
    bool PublishStatus();

private:
    static void StatusTimerCb(void* arg);

    DeepDogMqttClient* client_;
    DeepDogCapabilities caps_{};
    esp_timer_handle_t status_timer_ = nullptr;
    int http_port_ = 8080;
};
