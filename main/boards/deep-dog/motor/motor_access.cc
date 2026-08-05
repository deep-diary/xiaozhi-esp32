#include "motor/motor_access.h"

#if DEEP_DOG_MOTOR_ENABLE

#include "motor/deep_motor.h"

namespace {
DeepMotor* s_motor = nullptr;
}

void DeepDogMotorSet(DeepMotor* motor) {
    s_motor = motor;
}

DeepMotor* DeepDogMotorGet() {
    return s_motor;
}

#endif
