#pragma once

#include <string>
#include "config.h"

class DeepDogMqttClient;
class DeepMotor;
#if DEEP_DOG_MOTOR_ENABLE
#include "motor/protocol_motor.h"
#endif

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
#if DEEP_DOG_MOTOR_ENABLE
    static void DiscoveryCb(uint8_t motor_id, const motor_status_t& status, void* user_data);
    void PublishDiscoveryEvent(uint8_t motor_id, const motor_status_t& status);
#endif
    void ApplyCmd(const char* json);

    DeepDogMqttClient* client_;
    DeepMotor* motor_ = nullptr;
    bool enabled_ = false;
    bool connected_ = false;
    void* status_timer_ = nullptr;
    bool report_active_ = false;
};
