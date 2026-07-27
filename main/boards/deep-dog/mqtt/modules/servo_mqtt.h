#pragma once

#include <string>

class DeepDogMqttClient;

/** servo/cmd ↓ + servo/status ↑ retain */
class DeepDogServoMqtt {
public:
    explicit DeepDogServoMqtt(DeepDogMqttClient* client);
    ~DeepDogServoMqtt();

    void SetEnabled(bool enabled) { enabled_ = enabled; }

    void OnConnected();
    void OnDisconnected();
    void Stop();
    void OnMessage(const std::string& topic, const std::string& payload);

    bool PublishStatus(bool force = true);

private:
    static void ThrottleTimerCb(void* arg);
    static void BankNotifyCb(void* ctx);

    void OnBankNotify();
    void ScheduleStatusPublish();

    DeepDogMqttClient* client_;
    bool enabled_ = false;
    bool connected_ = false;
    void* throttle_timer_ = nullptr;  // esp_timer_handle_t
    int64_t last_publish_us_ = 0;
    bool pending_publish_ = false;
};
