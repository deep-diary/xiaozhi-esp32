#pragma once

/* 必须走 config.h：先定义 *_AVAILABLE，再 include board_features；
 * 若直接拉 board_features，未定义的 PWM_AVAILABLE 在 #if 里当 0，
 * 会把 DEEP_DOG_GIMBAL_ENABLE 钳成 0（见手柄 keymap 日志）。 */
#include "config.h"

#ifndef DEEP_DOG_HANDLE_ENABLE
#define DEEP_DOG_HANDLE_ENABLE 1
#endif

/** 板载 Bluepad32 + BTstack（默认关；与 NimBLE/BluFi 互斥，见 I02） */
#ifndef DEEP_DOG_HANDLE_BT_ENABLE
#define DEEP_DOG_HANDLE_BT_ENABLE 0
#endif

/** 接受 MQTT handle/input（PC 桥） */
#ifndef DEEP_DOG_HANDLE_MQTT_INPUT_ENABLE
#define DEEP_DOG_HANDLE_MQTT_INPUT_ENABLE 1
#endif

#ifndef DEEP_DOG_HANDLE_APP_LOG_ENABLE
#define DEEP_DOG_HANDLE_APP_LOG_ENABLE 1
#endif

#ifndef DEEP_DOG_HANDLE_APP_DOG_ENABLE
#define DEEP_DOG_HANDLE_APP_DOG_ENABLE 1
#endif

#ifndef DEEP_DOG_HANDLE_APP_SERVO_ENABLE
#define DEEP_DOG_HANDLE_APP_SERVO_ENABLE 0
#endif

#ifndef DEEP_DOG_HANDLE_APP_KEYMAP_ENABLE
#define DEEP_DOG_HANDLE_APP_KEYMAP_ENABLE 1
#endif

/** Dispatcher 轮询周期 */
#ifndef DEEP_DOG_HANDLE_DISPATCH_INTERVAL_US
#define DEEP_DOG_HANDLE_DISPATCH_INTERVAL_US 20000
#endif

#ifndef DEEP_DOG_HANDLE_EVENT_QUEUE_DEPTH
#define DEEP_DOG_HANDLE_EVENT_QUEUE_DEPTH 8
#endif

/** PC 桥无包超时后清零（ms） */
#ifndef DEEP_DOG_HANDLE_INPUT_TIMEOUT_MS
#define DEEP_DOG_HANDLE_INPUT_TIMEOUT_MS 500
#endif

/** status 发布最小间隔（ms）；过小易打满 Wi‑Fi，过大前端发粘 */
#ifndef DEEP_DOG_HANDLE_STATUS_MIN_INTERVAL_MS
#define DEEP_DOG_HANDLE_STATUS_MIN_INTERVAL_MS 25
#endif

/** 左摇杆死区（dog App） */
#ifndef DEEP_DOG_HANDLE_STICK_DEADZONE
#define DEEP_DOG_HANDLE_STICK_DEADZONE 0.25f
#endif

/** keymap 轴映射死区（I08b；可被 binding.deadzone 覆盖） */
#ifndef DEEP_DOG_HANDLE_AXIS_DEADZONE
#define DEEP_DOG_HANDLE_AXIS_DEADZONE 0.08f
#endif
