#ifndef DEEP_DOG_SERVO_H
#define DEEP_DOG_SERVO_H

#include "servo/servo_config.h"

#include "driver/mcpwm_prelude.h"
#include "esp_err.h"
#include "esp_timer.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SERVO_TYPE_90 = 0,
    SERVO_TYPE_180 = 1,
    SERVO_TYPE_270 = 2,
    SERVO_TYPE_360 = 3
} servo_type_t;

struct Servo_t;

/** 角度变化或运动结束时回调（在 esp_timer 任务或调用线程） */
typedef void (*ServoUpdateCb)(struct Servo_t* servo, void* ctx);

typedef struct Servo_t {
    mcpwm_oper_handle_t oper;
    mcpwm_cmpr_handle_t comparator;
    mcpwm_gen_handle_t generator;
    esp_timer_handle_t move_timer;
    int gpio_pin;
    servo_type_t type;
    int min_angle;
    int max_angle;
    int current_angle;
    int target_angle;
    int move_start_angle;
    int64_t move_start_us;
    uint32_t move_duration_ms;
    bool attached;
    bool moving;
    ServoUpdateCb on_update;
    void* on_update_ctx;
} Servo_t;

extern mcpwm_timer_handle_t g_servo_timer;
extern bool g_servo_timer_initialized;

esp_err_t Servo_attach(Servo_t* servo, int gpio_pin, servo_type_t type);
void Servo_detach(Servo_t* servo);
/** 立即到位（duration=0）；会取消进行中的插值 */
void Servo_write(Servo_t* servo, int angle);
/**
 * 在 duration_ms 内插值到 angle。
 * duration_ms==0：立即到位（最大速度）。
 * 新指令取消同路未完成运动。
 */
void Servo_writeTimed(Servo_t* servo, int angle, uint32_t duration_ms);
int Servo_read(Servo_t* servo);
int Servo_target(Servo_t* servo);
bool Servo_attached(Servo_t* servo);
bool Servo_isMoving(Servo_t* servo);
void Servo_setAngleRange(Servo_t* servo, int min_angle, int max_angle);
void Servo_setType(Servo_t* servo, servo_type_t type);
void Servo_setUpdateCallback(Servo_t* servo, ServoUpdateCb cb, void* ctx);

#ifdef __cplusplus
}
#endif

#endif
