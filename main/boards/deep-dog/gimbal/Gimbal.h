#ifndef DEEP_DOG_GIMBAL_H
#define DEEP_DOG_GIMBAL_H

#include "servo/Servo.h"
#include "gimbal/gimbal_config.h"
#include "esp_err.h"
#include "esp_timer.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GIMBAL_DIR_LEFT = 0,
    GIMBAL_DIR_RIGHT,
    GIMBAL_DIR_UP,
    GIMBAL_DIR_DOWN,
} gimbal_dir_t;

typedef struct {
    int pan;
    int tilt;
    int pan_speed;
    int tilt_speed;
    int step_deg;
    bool moving_pan;
    bool moving_tilt;
    bool ready;
    int lim_pan_min;
    int lim_pan_max;
    int lim_tilt_min;
    int lim_tilt_max;
} GimbalStatus_t;

typedef void (*GimbalNotifyCb)(void* ctx);

typedef struct {
    Servo_t pan_servo;
    Servo_t tilt_servo;
    bool initialized;
    int pan_speed;
    int tilt_speed;
    int step_deg;
    int jog_pan_dir;   // -1 / 0 / +1 (discrete hold)
    int jog_tilt_dir;  // -1 / 0 / +1
    float analog_pan_u;   // stick rate [-1,1]; 0 = inactive (overrides discrete when nonzero)
    float analog_tilt_u;
    esp_timer_handle_t jog_timer;
    GimbalNotifyCb on_notify;
    void* on_notify_ctx;
} Gimbal_t;

esp_err_t Gimbal_init(Gimbal_t* gimbal, int pan_gpio, int tilt_gpio);
void Gimbal_deinit(Gimbal_t* gimbal);
bool Gimbal_isInitialized(Gimbal_t* gimbal);

void Gimbal_setAngles(Gimbal_t* gimbal, int pan_angle, int tilt_angle);
void Gimbal_setAnglesTimed(Gimbal_t* gimbal, int pan_angle, int tilt_angle, int speed_deg_s);
void Gimbal_moveRelative(Gimbal_t* gimbal, int d_pan, int d_tilt, int speed_deg_s);

void Gimbal_nudgeLeft(Gimbal_t* gimbal);
void Gimbal_nudgeRight(Gimbal_t* gimbal);
void Gimbal_nudgeUp(Gimbal_t* gimbal);
void Gimbal_nudgeDown(Gimbal_t* gimbal);

void Gimbal_startJog(Gimbal_t* gimbal, gimbal_dir_t dir);
void Gimbal_stopJog(Gimbal_t* gimbal);
void Gimbal_stop(Gimbal_t* gimbal);

/** 回中复位：行程中点角 + 默认速度/步进；清 jog/模拟（等同上电后姿态） */
void Gimbal_home(Gimbal_t* gimbal);

/** Stick axis rate ∈ [-1,1]; 0 clears that axis analog. Effective speed = |u| * axis speed. */
void Gimbal_setPanRate(Gimbal_t* gimbal, float u);
void Gimbal_setTiltRate(Gimbal_t* gimbal, float u);

void Gimbal_setPanSpeed(Gimbal_t* gimbal, int speed_deg_s);
void Gimbal_setTiltSpeed(Gimbal_t* gimbal, int speed_deg_s);
void Gimbal_panSpeedUp(Gimbal_t* gimbal);
void Gimbal_panSpeedDown(Gimbal_t* gimbal);
void Gimbal_tiltSpeedUp(Gimbal_t* gimbal);
void Gimbal_tiltSpeedDown(Gimbal_t* gimbal);

bool Gimbal_getStatus(Gimbal_t* gimbal, GimbalStatus_t* out);
void Gimbal_setNotifyCallback(Gimbal_t* gimbal, GimbalNotifyCb cb, void* ctx);

/** 板级单例（MQTT / keymap） */
esp_err_t DeepDogGimbalInit(void);
void DeepDogGimbalDeinit(void);
bool DeepDogGimbalReady(void);
Gimbal_t* DeepDogGimbalGet(void);

#ifdef __cplusplus
}
#endif

#endif
