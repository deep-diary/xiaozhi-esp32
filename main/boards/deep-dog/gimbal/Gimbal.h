#ifndef DEEP_DOG_GIMBAL_H
#define DEEP_DOG_GIMBAL_H

#include "servo/Servo.h"
#include "gimbal/gimbal_config.h"
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Servo_t pan_servo;
    Servo_t tilt_servo;
    bool initialized;
} Gimbal_t;

esp_err_t Gimbal_init(Gimbal_t *gimbal, int pan_gpio, int tilt_gpio);
void Gimbal_deinit(Gimbal_t *gimbal);
void Gimbal_setAngles(Gimbal_t *gimbal, int pan_angle, int tilt_angle);
bool Gimbal_isInitialized(Gimbal_t *gimbal);

#ifdef __cplusplus
}
#endif

#endif
