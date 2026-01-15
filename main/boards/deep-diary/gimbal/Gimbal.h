/*
 * Gimbal.h - ESP32 云台控制类
 * 管理多个舵机组成的云台系统
 */

#ifndef GIMBAL_H
#define GIMBAL_H

#include "../servo/Servo.h"
#include "driver/twai.h"

#ifdef __cplusplus
extern "C" {
#endif

// CAN舵机控制命令ID
#define CAN_CMD_SERVO_CONTROL 0x200

// 使用ESP-IDF的twai_message_t作为CanFrame
typedef twai_message_t CanFrame;

// 云台类结构
typedef struct {
    Servo_t pan_servo;        // 水平舵机
    Servo_t tilt_servo;       // 垂直舵机
    bool initialized;         // 是否已初始化
} Gimbal_t;

// 云台类方法
esp_err_t Gimbal_init(Gimbal_t *gimbal, int pan_gpio, int tilt_gpio);
void Gimbal_deinit(Gimbal_t *gimbal);
void Gimbal_setAngles(Gimbal_t *gimbal, int pan_angle, int tilt_angle);
void Gimbal_getAngles(Gimbal_t *gimbal, int *pan_angle, int *tilt_angle);
void Gimbal_setPanAngle(Gimbal_t *gimbal, int angle);
void Gimbal_setTiltAngle(Gimbal_t *gimbal, int angle);
int Gimbal_getPanAngle(Gimbal_t *gimbal);
int Gimbal_getTiltAngle(Gimbal_t *gimbal);
bool Gimbal_isInitialized(Gimbal_t *gimbal);

// 云台方向控制函数
void Gimbal_moveUp(Gimbal_t *gimbal, int angle);
void Gimbal_moveDown(Gimbal_t *gimbal, int angle);
void Gimbal_moveLeft(Gimbal_t *gimbal, int angle);
void Gimbal_moveRight(Gimbal_t *gimbal, int angle);

// 云台测试函数
void Gimbal_testSweep(Gimbal_t *gimbal, uint32_t duration_ms, int step_size);
void Gimbal_testPanSweep(Gimbal_t *gimbal, uint32_t duration_ms, int step_size);
void Gimbal_testTiltSweep(Gimbal_t *gimbal, uint32_t duration_ms, int step_size);
void Gimbal_testBothSweep(Gimbal_t *gimbal, uint32_t duration_ms, int step_size);

// CAN舵机控制处理函数
void Gimbal_handleCanCommand(Gimbal_t *gimbal, const CanFrame* frame);

#ifdef __cplusplus
}
#endif

#endif // GIMBAL_H
