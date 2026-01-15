#ifndef DEEP_MOTOR_CONTROL_H
#define DEEP_MOTOR_CONTROL_H

#include "mcp_server.h"
#include "deep_motor.h"

class DeepMotorControl {
private:
    DeepMotor* deep_motor_;

public:
    explicit DeepMotorControl(DeepMotor* deep_motor, McpServer& mcp_server);
    ~DeepMotorControl() = default;
}; 

#endif // DEEP_MOTOR_CONTROL_H
