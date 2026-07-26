#include "servo/Servo.h"

#include <esp_log.h>
#include <stdlib.h>

#define TAG "dog_servo"

#if DEEP_DOG_SERVO_ENABLE

esp_err_t Servo_attach(Servo_t *servo, int gpio_pin, servo_type_t type) {
    if (!servo) {
        return ESP_ERR_INVALID_ARG;
    }
    servo->gpio_pin = gpio_pin;
    servo->type = type;
    servo->min_angle = 0;
    switch (type) {
        case SERVO_TYPE_90:
            servo->max_angle = 90;
            break;
        case SERVO_TYPE_270:
            servo->max_angle = 270;
            break;
        case SERVO_TYPE_360:
            servo->max_angle = 360;
            break;
        case SERVO_TYPE_180:
        default:
            servo->max_angle = 180;
            break;
    }
    servo->current_angle = servo->max_angle / 2;
    servo->attached = true;
    ESP_LOGI(TAG, "Servo placeholder attach gpio=%d type=%d (MCPWM TODO)", gpio_pin, (int)type);
    return ESP_OK;
}

void Servo_detach(Servo_t *servo) {
    if (servo) {
        servo->attached = false;
    }
}

void Servo_write(Servo_t *servo, int angle) {
    if (!servo || !servo->attached) {
        return;
    }
    if (angle < servo->min_angle) {
        angle = servo->min_angle;
    }
    if (angle > servo->max_angle) {
        angle = servo->max_angle;
    }
    servo->current_angle = angle;
}

int Servo_read(Servo_t *servo) {
    return (servo && servo->attached) ? servo->current_angle : 0;
}

bool Servo_attached(Servo_t *servo) {
    return servo && servo->attached;
}

#else

esp_err_t Servo_attach(Servo_t *servo, int gpio_pin, servo_type_t type) {
    (void)servo;
    (void)gpio_pin;
    (void)type;
    return ESP_ERR_NOT_SUPPORTED;
}

void Servo_detach(Servo_t *servo) { (void)servo; }
void Servo_write(Servo_t *servo, int angle) {
    (void)servo;
    (void)angle;
}
int Servo_read(Servo_t *servo) {
    (void)servo;
    return 0;
}
bool Servo_attached(Servo_t *servo) {
    (void)servo;
    return false;
}

#endif
