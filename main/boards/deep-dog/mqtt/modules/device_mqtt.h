#pragma once

#include <esp_timer.h>

class DeepDogMqttClient;

struct DeepDogCapabilities {
    bool dog = false;
    bool motor = false;
    bool stream = false;
    bool face = false;
    bool track = false;
    bool imu = false;
    bool led = false;
    bool servo = false;
    bool gimbal = false;
    bool handle = false;
    bool touch = true;
    bool can = false;
    bool arm = false;
    bool uart = false;
    bool ws_mcp = false;
};

/** device/info（retain）+ device/status 心跳 */
class DeepDogDeviceMqtt {
public:
    explicit DeepDogDeviceMqtt(DeepDogMqttClient* client);
    ~DeepDogDeviceMqtt();

    void SetCapabilities(const DeepDogCapabilities& caps) { caps_ = caps; }
    void SetHttpPort(int port) { http_port_ = port; }
    void SetWsMcpEndpoint(int port, const char* path);
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
    int ws_mcp_port_ = 0;
    const char* ws_mcp_path_ = "/ws";
};
