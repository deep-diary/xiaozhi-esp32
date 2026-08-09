#ifndef _DEEP_DOG_BOARD_FEATURES_H_
#define _DEEP_DOG_BOARD_FEATURES_H_

/**
 * 产品功能组合（同硬件只改本文件 + config.h 的 EXT_PIN_MODE）。
 * 须在 config.h 定义 *_AVAILABLE 之后 include。
 * 剖面表见 FEATURE_FLAGS.md。
 *
 * 依赖：CAN_AVAILABLE → CAN → MOTOR → DOG | ARM
 *       PWM_AVAILABLE → SERVO → GIMBAL
 *       LED_AVAILABLE → LED（EXT_PIN=LED，DIN=gpio_a）
 */

/* -------- 总线 / 驱动（CAN 等默认关；舵机见下方 PWM） -------- */
#ifndef DEEP_DOG_CAN_ENABLE
#define DEEP_DOG_CAN_ENABLE 0
#endif
#ifndef DEEP_DOG_MOTOR_ENABLE
#define DEEP_DOG_MOTOR_ENABLE 0
#endif
#ifndef DEEP_DOG_UART_ENABLE
#define DEEP_DOG_UART_ENABLE 0
#endif
#ifndef DEEP_DOG_RS485_ENABLE
#define DEEP_DOG_RS485_ENABLE 0
#endif
#ifndef DEEP_DOG_IO_ENABLE
#define DEEP_DOG_IO_ENABLE 0
#endif
#ifndef DEEP_DOG_AD_ENABLE
#define DEEP_DOG_AD_ENABLE 0
#endif

/* -------- 产品应用 -------- */
#ifndef DEEP_DOG_DOG_ENABLE
#define DEEP_DOG_DOG_ENABLE 0
#endif
#ifndef DEEP_DOG_ARM_ENABLE
#define DEEP_DOG_ARM_ENABLE 0
#endif

/* -------- PWM 产品（需 EXT_PIN=PWM；默认开舵机调试） -------- */
#ifndef DEEP_DOG_SERVO_ENABLE
#define DEEP_DOG_SERVO_ENABLE 1
#endif
#ifndef DEEP_DOG_GIMBAL_ENABLE
#define DEEP_DOG_GIMBAL_ENABLE 0
#endif

/* -------- 非引出脚功能 -------- */
#ifndef DEEP_DOG_MQTT_ENABLE
#define DEEP_DOG_MQTT_ENABLE 1
#endif
#ifndef DEEP_DOG_TRACK_MQTT_ENABLE
#define DEEP_DOG_TRACK_MQTT_ENABLE 1
#endif
#ifndef DEEP_DOG_VISION_HUB_ENABLE
#define DEEP_DOG_VISION_HUB_ENABLE 1
#endif
#ifndef DEEP_DOG_FACE_AI_ENABLE
#define DEEP_DOG_FACE_AI_ENABLE 1
#endif
#ifndef DEEP_DOG_IMU_ENABLE
#define DEEP_DOG_IMU_ENABLE 1
#endif
#ifndef DEEP_DOG_HTTP_SERVER_ENABLE
#define DEEP_DOG_HTTP_SERVER_ENABLE 0
#endif
#ifndef DEEP_DOG_LED_ENABLE
#define DEEP_DOG_LED_ENABLE 1
#endif

/* -------- 蓝牙手柄（bluepad32；默认关，配对联调不受影响） -------- */
#ifndef DEEP_DOG_HANDLE_ENABLE
#define DEEP_DOG_HANDLE_ENABLE 0
#endif

/* -------- 依赖钳位 -------- */
#if !DEEP_DOG_CAN_AVAILABLE
#undef DEEP_DOG_CAN_ENABLE
#define DEEP_DOG_CAN_ENABLE 0
#endif
#if !DEEP_DOG_CAN_ENABLE
#undef DEEP_DOG_MOTOR_ENABLE
#define DEEP_DOG_MOTOR_ENABLE 0
#endif
#if !DEEP_DOG_MOTOR_ENABLE
#undef DEEP_DOG_DOG_ENABLE
#define DEEP_DOG_DOG_ENABLE 0
#undef DEEP_DOG_ARM_ENABLE
#define DEEP_DOG_ARM_ENABLE 0
#endif

#if !DEEP_DOG_UART_AVAILABLE
#undef DEEP_DOG_UART_ENABLE
#define DEEP_DOG_UART_ENABLE 0
#endif
#if !DEEP_DOG_RS485_AVAILABLE
#undef DEEP_DOG_RS485_ENABLE
#define DEEP_DOG_RS485_ENABLE 0
#endif
#if !DEEP_DOG_IO_AVAILABLE
#undef DEEP_DOG_IO_ENABLE
#define DEEP_DOG_IO_ENABLE 0
#endif
#if !DEEP_DOG_AD_AVAILABLE
#undef DEEP_DOG_AD_ENABLE
#define DEEP_DOG_AD_ENABLE 0
#endif

#if !DEEP_DOG_PWM_AVAILABLE
#undef DEEP_DOG_SERVO_ENABLE
#define DEEP_DOG_SERVO_ENABLE 0
#undef DEEP_DOG_GIMBAL_ENABLE
#define DEEP_DOG_GIMBAL_ENABLE 0
#endif
#if !DEEP_DOG_SERVO_ENABLE
#undef DEEP_DOG_GIMBAL_ENABLE
#define DEEP_DOG_GIMBAL_ENABLE 0
#endif

#if !DEEP_DOG_LED_AVAILABLE
#undef DEEP_DOG_LED_ENABLE
#define DEEP_DOG_LED_ENABLE 0
#endif

#endif  // _DEEP_DOG_BOARD_FEATURES_H_
