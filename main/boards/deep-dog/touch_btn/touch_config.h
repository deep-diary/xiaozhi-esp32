#ifndef _DEEP_DOG_TOUCH_CONFIG_H_
#define _DEEP_DOG_TOUCH_CONFIG_H_

#include "config.h"

/**
 * 触摸按键行为相关配置（从 board `config.h` 拆分）。
 */

/** 驱动轮询周期（ms）；长按 cycles = DEEP_DOG_TOUCH_LONG_PRESS_MS / 本值 */
#ifndef DEEP_DOG_TOUCH_POLL_MS
#define DEEP_DOG_TOUCH_POLL_MS 50
#endif

/** 去抖确认周期数（press / release） */
#ifndef DEEP_DOG_TOUCH_DEBOUNCE_CYCLES
#define DEEP_DOG_TOUCH_DEBOUNCE_CYCLES 2
#endif

/** 长按判定时长（ms） */
#ifndef DEEP_DOG_TOUCH_LONG_PRESS_MS
#define DEEP_DOG_TOUCH_LONG_PRESS_MS 1000
#endif

/** 双击判定窗口（ms）；首次抬起后等待，超时补发 short_press */
#ifndef DEEP_DOG_TOUCH_DOUBLE_MS
#define DEEP_DOG_TOUCH_DOUBLE_MS 350
#endif

/** 事件队列深度（TouchEventHub） */
#ifndef DEEP_DOG_TOUCH_EVENT_QUEUE_DEPTH
#define DEEP_DOG_TOUCH_EVENT_QUEUE_DEPTH 32
#endif

/** 板级调度器 Poll 周期（us） */
#ifndef DEEP_DOG_TOUCH_DISPATCH_INTERVAL_US
#define DEEP_DOG_TOUCH_DISPATCH_INTERVAL_US 20000
#endif

/** MQTT touch/status 是否带 debug[]（raw/baseline/abs_diff） */
#ifndef DEEP_DOG_TOUCH_MQTT_DEBUG
#define DEEP_DOG_TOUCH_MQTT_DEBUG 0
#endif

/**
 * 触摸触发「先 Capture 再 Explain」（与 MCP `self.camera.take_photo` 一致）：
 * - 键 1：short_press 时排队；
 * - 键 2：成功 goForward 一小步后排队。
 * 需已配置图像解释 URL/Token。置 0 可关闭。
 */
#ifndef DEEP_DOG_TOUCH2_SHORT_PHOTO_EXPLAIN
#define DEEP_DOG_TOUCH2_SHORT_PHOTO_EXPLAIN 0
#endif

/** 按键应用：LOG 常开；DOG/SERVO 跟随产品 ENABLE */
#ifndef DEEP_DOG_TOUCH_APP_LOG_ENABLE
#define DEEP_DOG_TOUCH_APP_LOG_ENABLE 1
#endif

/** 狗触摸应用（含可选拍照解释）；运控逻辑仍受 DEEP_DOG_DOG_ENABLE 约束 */
#ifndef DEEP_DOG_TOUCH_APP_DOG_ENABLE
#define DEEP_DOG_TOUCH_APP_DOG_ENABLE 1
#endif

#ifndef DEEP_DOG_TOUCH_APP_SERVO_ENABLE
#define DEEP_DOG_TOUCH_APP_SERVO_ENABLE DEEP_DOG_SERVO_ENABLE
#endif

/** 跨键组合识别（TouchComboRecognizer）；0=不编译进 Poll 路径 */
#ifndef DEEP_DOG_TOUCH_COMBO_ENABLE
#define DEEP_DOG_TOUCH_COMBO_ENABLE 1
#endif

/** 组合命中后是否跳过应用 fan-out；阶段一保持 0，避免影响 dog */
#ifndef DEEP_DOG_TOUCH_COMBO_CONSUME
#define DEEP_DOG_TOUCH_COMBO_CONSUME 0
#endif

#endif  // _DEEP_DOG_TOUCH_CONFIG_H_
