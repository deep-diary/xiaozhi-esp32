#pragma once

#include "config.h"

#if DEEP_DOG_MOTOR_ENABLE
class DeepMotor;

/** 板级 DeepMotor 单例访问（MQTT / keymap 共用） */
void DeepDogMotorSet(DeepMotor* motor);
DeepMotor* DeepDogMotorGet();
#endif
