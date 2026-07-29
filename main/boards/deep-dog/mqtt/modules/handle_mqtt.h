#pragma once

#include "handle/handle_event_hub.h"

#include <esp_timer.h>

#include <string>

class DeepDogMqttClient;

/** handle/status ↑ + handle/input ↓ + handle/cmd ↓ */
class DeepDogHandleMqtt {
public:
    explicit DeepDogHandleMqtt(DeepDogMqttClient* client);
    ~DeepDogHandleMqtt();

    void SetEnabled(bool enabled) { enabled_ = enabled; }
    void SetHub(HandleEventHub* hub) { hub_ = hub; }

    void OnConnected();
    void OnDisconnected();
    void Stop();

    void OnMessage(const std::string& topic, const std::string& payload);

    /** Hub Push 同步回调（可节流发布） */
    void OnSnapshot(const HandleSnapshot& snap);

    bool PublishStatus();

private:
    static void TimeoutTimerCb(void* arg);
    void EnsureTimeoutTimer();
    void ArmInputTimeout();
    void OnInputTimeout();
    void HandleCmd(const std::string& payload);
    void HandleInput(const std::string& payload);
    bool ParseSnapshotJson(const std::string& payload, HandleSnapshot* out);

    DeepDogMqttClient* client_;
    HandleEventHub* hub_ = nullptr;
    esp_timer_handle_t timeout_timer_ = nullptr;
    bool enabled_ = false;
    bool connected_ = false;
    int64_t last_publish_us_ = 0;
};
