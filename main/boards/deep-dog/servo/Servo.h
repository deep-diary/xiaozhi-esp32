#ifndef DEEP_DOG_SERVO_H
#define DEEP_DOG_SERVO_H

#include "servo/servo_config.h"
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SERVO_TYPE_90 = 0,
    SERVO_TYPE_180 = 1,
    SERVO_TYPE_270 = 2,
    SERVO_TYPE_360 = 3
} servo_type_t;

typedef struct {
    int gpio_pin;
    servo_type_t type;
    int min_angle;
    int max_angle;
    int current_angle;
    bool attached;
} Servo_t;

esp_err_t Servo_attach(Servo_t *servo, int gpio_pin, servo_type_t type);
void Servo_detach(Servo_t *servo);
void Servo_write(Servo_t *servo, int angle);
int Servo_read(Servo_t *servo);
bool Servo_attached(Servo_t *servo);

#ifdef __cplusplus
}
#endif

#endif
