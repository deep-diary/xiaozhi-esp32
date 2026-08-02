#pragma once

#include <string>

class DeepDogMqttClient;

/** gimbal/cmd ↓ + gimbal/status ↑ retain */
class DeepDogGimbalMqtt {
public:
    explicit DeepDogGimbalMqtt(DeepDogMqttClient* client);
    ~DeepDogGimbalMqtt();

    void SetEnabled(bool enabled) { enabled_ = enabled; }

    void OnConnected();
    void OnDisconnected();
    void Stop();
    void OnMessage(const std::string& topic, const std::string& payload);

    bool PublishStatus(bool force = true);

private:
    static void ThrottleTimerCb(void* arg);
    static void NotifyCb(void* ctx);

    void OnGimbalNotify();
    void ScheduleStatusPublish();

    DeepDogMqttClient* client_;
    bool enabled_ = false;
    bool connected_ = false;
    void* throttle_timer_ = nullptr;
    int64_t last_publish_us_ = 0;
    bool pending_publish_ = false;
};
