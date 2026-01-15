/*
 * Servo.h - ESP32 舵机控制类
 * 支持不同角度范围的舵机控制
 */

#ifndef SERVO_H
#define SERVO_H

#include "esp_timer.h"
#include "driver/mcpwm_prelude.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// 舵机配置参数
#define SERVO_MIN_PULSEWIDTH_US 500   // 最小脉冲宽度 (0.5ms)
#define SERVO_MAX_PULSEWIDTH_US 2500  // 最大脉冲宽度 (2.5ms)
#define SERVO_TIMEBASE_RESOLUTION_HZ 1000000  // 1MHz, 1us per tick
#define SERVO_TIMEBASE_PERIOD 20000   // 20000 ticks, 20ms (50Hz)

// 舵机类型枚举 - 按角度范围分类
typedef enum {
    SERVO_TYPE_90 = 0,   // 90度舵机 (0-90度)
    SERVO_TYPE_180 = 1,  // 180度舵机 (0-180度)
    SERVO_TYPE_270 = 2,  // 270度舵机 (0-270度)
    SERVO_TYPE_360 = 3   // 360度舵机 (0-360度)
} servo_type_t;

// 舵机类结构
typedef struct {
    mcpwm_timer_handle_t timer;
    mcpwm_oper_handle_t oper;
    mcpwm_cmpr_handle_t comparator;
    mcpwm_gen_handle_t generator;
    int gpio_pin;
    servo_type_t type;             // 舵机类型
    int min_angle;                 // 最小角度
    int max_angle;                 // 最大角度
    int current_angle;             // 当前角度
    bool attached;                 // 是否已绑定
} Servo_t;

// 全局定时器（所有舵机共享）
extern mcpwm_timer_handle_t g_servo_timer;
extern bool g_servo_timer_initialized;

// 舵机类方法
esp_err_t Servo_attach(Servo_t *servo, int gpio_pin, servo_type_t type);
void Servo_detach(Servo_t *servo);
void Servo_write(Servo_t *servo, int angle);
int Servo_read(Servo_t *servo);
bool Servo_attached(Servo_t *servo);
void Servo_setAngleRange(Servo_t *servo, int min_angle, int max_angle);

// 舵机测试函数
void Servo_testSweep(Servo_t *servo, uint32_t duration_ms, int step_size);

#ifdef __cplusplus
}
#endif

#endif // SERVO_H
