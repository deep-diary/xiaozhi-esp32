#pragma once

#include <string>

class DeepDogMqttClient;
class DeepMotor;

/** motor/cmd ↓ + motor/status ↑（MOT-03 / 14-motor） */
class DeepDogMotorMqtt {
public:
    explicit DeepDogMotorMqtt(DeepDogMqttClient* client);
    ~DeepDogMotorMqtt();

    void SetEnabled(bool enabled) { enabled_ = enabled; }
    void SetMotor(DeepMotor* motor);

    void OnConnected();
    void OnDisconnected();
    void Stop();
    void OnMessage(const std::string& topic, const std::string& payload);

    bool PublishStatus(bool force = true);

private:
    static void StatusTimerCb(void* arg);
    void ApplyCmd(const char* json);

    DeepDogMqttClient* client_;
    DeepMotor* motor_ = nullptr;
    bool enabled_ = false;
    bool connected_ = false;
    void* status_timer_ = nullptr;
};
