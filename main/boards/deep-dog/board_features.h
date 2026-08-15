#ifndef _DEEP_DOG_BOARD_FEATURES_H_
#define _DEEP_DOG_BOARD_FEATURES_H_

/**
 * 产品功能组合（同硬件只改本文件 + config.h 的 EXT_PIN_MODE）。
 * 须在 config.h 定义 *_AVAILABLE 之后 include。
 * 剖面表见 FEATURE_FLAGS.md。
 *
 * 38/48 成对引出脚同一时刻只有一种 EXT_PIN_MODE（config.h），互斥。
 * *_ENABLE 只表示是否编入对应软件栈；无 *_AVAILABLE 时由下方钳位强制为 0。
 *
 * 依赖链：
 *   CAN_AVAILABLE → CAN_ENABLE → MOTOR_ENABLE → DOG | ARM
 *   PWM_AVAILABLE → SERVO | GIMBAL
 *   IO_AVAILABLE  → IO（纯 GPIO，与 LED 模式互斥）
 *   LED_AVAILABLE → LED（WS2812；DIN 默认 gpio_a，可在 config.h 覆盖 LED_STRIP_GPIO）
 */

/* -------- 引出脚外设（须 config.h EXT_PIN_MODE 匹配；A/B 默认 38/48） -------- */
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
/** EXT_PIN=IO：两路普通 GPIO 输入/输出（非 WS2812；灯带用 LED_ENABLE） */
#ifndef DEEP_DOG_IO_ENABLE
#define DEEP_DOG_IO_ENABLE 0
#endif
#ifndef DEEP_DOG_AD_ENABLE
#define DEEP_DOG_AD_ENABLE 0
#endif
/** EXT_PIN=LED：WS2812 灯带（DIN 默认 gpio_a；脚位见 config.h DEEP_DOG_LED_STRIP_GPIO） */
#ifndef DEEP_DOG_LED_ENABLE
#define DEEP_DOG_LED_ENABLE 1
#endif

/* -------- CAN 产品链（须 CAN+MOTOR） -------- */
#ifndef DEEP_DOG_DOG_ENABLE
#define DEEP_DOG_DOG_ENABLE 0
#endif
#ifndef DEEP_DOG_ARM_ENABLE
#define DEEP_DOG_ARM_ENABLE 0
#endif

/* -------- 引出脚 PWM 产品（须 EXT_PIN=PWM） -------- */
#ifndef DEEP_DOG_SERVO_ENABLE
#define DEEP_DOG_SERVO_ENABLE 0
#endif
#ifndef DEEP_DOG_GIMBAL_ENABLE
#define DEEP_DOG_GIMBAL_ENABLE 0
#endif

/* -------- 与 38/48 引出脚无关 -------- */
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
#ifndef DEEP_DOG_HANDLE_ENABLE
#define DEEP_DOG_HANDLE_ENABLE 1
#endif

/* -------- 依赖钳位（*_AVAILABLE 必须已由 config.h 定义；未定义时不钳位） -------- */
#if defined(DEEP_DOG_CAN_AVAILABLE) && !DEEP_DOG_CAN_AVAILABLE
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

#if defined(DEEP_DOG_UART_AVAILABLE) && !DEEP_DOG_UART_AVAILABLE
#undef DEEP_DOG_UART_ENABLE
#define DEEP_DOG_UART_ENABLE 0
#endif
#if defined(DEEP_DOG_RS485_AVAILABLE) && !DEEP_DOG_RS485_AVAILABLE
#undef DEEP_DOG_RS485_ENABLE
#define DEEP_DOG_RS485_ENABLE 0
#endif
#if defined(DEEP_DOG_IO_AVAILABLE) && !DEEP_DOG_IO_AVAILABLE
#undef DEEP_DOG_IO_ENABLE
#define DEEP_DOG_IO_ENABLE 0
#endif
#if defined(DEEP_DOG_AD_AVAILABLE) && !DEEP_DOG_AD_AVAILABLE
#undef DEEP_DOG_AD_ENABLE
#define DEEP_DOG_AD_ENABLE 0
#endif
#if defined(DEEP_DOG_LED_AVAILABLE) && !DEEP_DOG_LED_AVAILABLE
#undef DEEP_DOG_LED_ENABLE
#define DEEP_DOG_LED_ENABLE 0
#endif

#if defined(DEEP_DOG_PWM_AVAILABLE) && !DEEP_DOG_PWM_AVAILABLE
#undef DEEP_DOG_SERVO_ENABLE
#define DEEP_DOG_SERVO_ENABLE 0
#undef DEEP_DOG_GIMBAL_ENABLE
#define DEEP_DOG_GIMBAL_ENABLE 0
#endif
/* 同脚互斥：同时开时优先云台；底层 Servo 驱动随 GIMBAL|SERVO 任一开启 */
#if DEEP_DOG_GIMBAL_ENABLE && DEEP_DOG_SERVO_ENABLE
#undef DEEP_DOG_SERVO_ENABLE
#define DEEP_DOG_SERVO_ENABLE 0
#endif

#endif  // _DEEP_DOG_BOARD_FEATURES_H_
