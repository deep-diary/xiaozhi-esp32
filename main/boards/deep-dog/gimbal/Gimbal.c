#include "gimbal/Gimbal.h"

#include <esp_log.h>

#define TAG "dog_gimbal"

#if DEEP_DOG_GIMBAL_ENABLE

esp_err_t Gimbal_init(Gimbal_t *gimbal, int pan_gpio, int tilt_gpio) {
    if (!gimbal) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = Servo_attach(&gimbal->pan_servo, pan_gpio, SERVO_TYPE_270);
    if (err != ESP_OK) {
        return err;
    }
    err = Servo_attach(&gimbal->tilt_servo, tilt_gpio, SERVO_TYPE_180);
    if (err != ESP_OK) {
        Servo_detach(&gimbal->pan_servo);
        return err;
    }
    gimbal->initialized = true;
    ESP_LOGI(TAG, "Gimbal placeholder pan=%d tilt=%d", pan_gpio, tilt_gpio);
    return ESP_OK;
}

void Gimbal_deinit(Gimbal_t *gimbal) {
    if (!gimbal) {
        return;
    }
    Servo_detach(&gimbal->pan_servo);
    Servo_detach(&gimbal->tilt_servo);
    gimbal->initialized = false;
}

void Gimbal_setAngles(Gimbal_t *gimbal, int pan_angle, int tilt_angle) {
    if (!gimbal || !gimbal->initialized) {
        return;
    }
    Servo_write(&gimbal->pan_servo, pan_angle);
    Servo_write(&gimbal->tilt_servo, tilt_angle);
}

bool Gimbal_isInitialized(Gimbal_t *gimbal) {
    return gimbal && gimbal->initialized;
}

#else

esp_err_t Gimbal_init(Gimbal_t *gimbal, int pan_gpio, int tilt_gpio) {
    (void)gimbal;
    (void)pan_gpio;
    (void)tilt_gpio;
    return ESP_ERR_NOT_SUPPORTED;
}

void Gimbal_deinit(Gimbal_t *gimbal) { (void)gimbal; }
void Gimbal_setAngles(Gimbal_t *gimbal, int pan_angle, int tilt_angle) {
    (void)gimbal;
    (void)pan_angle;
    (void)tilt_angle;
}
bool Gimbal_isInitialized(Gimbal_t *gimbal) {
    (void)gimbal;
    return false;
}

#endif
