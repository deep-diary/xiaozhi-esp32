#pragma once

#include <string>

#include "config.h"
#if DEEP_DOG_MOTOR_ENABLE
#include "motor/protocol_motor.h"
#endif

class DeepDogMqttClient;
class DeepMotor;

/** motor/cmd ↓ + motor/status ↑ + motor/tools/mcp_result（MOT-03 / MOT-10） */
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

    static void OnMotorDiscovered(uint8_t motor_id, const motor_status_t& status, void* user_data);

private:
    static void StatusTimerCb(void* arg);
    void ApplyCmd(const char* json);
    bool PublishTools();
    bool PublishTeachingSnapshot(uint8_t motor_id);
    bool PublishTeachingStatus();
    void PublishMcpResult(const char* tool_name, bool ok, const char* result_json, const char* error);

    DeepDogMqttClient* client_;
    DeepMotor* motor_ = nullptr;
    bool enabled_ = false;
    bool connected_ = false;
    void* status_timer_ = nullptr;
};
