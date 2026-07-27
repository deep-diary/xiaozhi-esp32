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

/* ---- 检测灵敏度出厂默认（可 MQTT/NVS 覆盖） ---- */
#ifndef DEEP_DOG_TOUCH_PRESS_ABS_RATIO
#define DEEP_DOG_TOUCH_PRESS_ABS_RATIO 0.05f
#endif
#ifndef DEEP_DOG_TOUCH_RELEASE_ABS_RATIO
#define DEEP_DOG_TOUCH_RELEASE_ABS_RATIO 0.03f
#endif
#ifndef DEEP_DOG_TOUCH_PRESS_ABS_OFFSET
#define DEEP_DOG_TOUCH_PRESS_ABS_OFFSET 200u
#endif
#ifndef DEEP_DOG_TOUCH_RELEASE_ABS_OFFSET
#define DEEP_DOG_TOUCH_RELEASE_ABS_OFFSET 120u
#endif
/** 键1/2/3 按下绝对下限（键2/3 已相对旧值下调） */
#ifndef DEEP_DOG_TOUCH_PRESS_ABS_MIN_1
#define DEEP_DOG_TOUCH_PRESS_ABS_MIN_1 2600u
#endif
#ifndef DEEP_DOG_TOUCH_PRESS_ABS_MIN_2
#define DEEP_DOG_TOUCH_PRESS_ABS_MIN_2 800u
#endif
#ifndef DEEP_DOG_TOUCH_PRESS_ABS_MIN_3
#define DEEP_DOG_TOUCH_PRESS_ABS_MIN_3 500u
#endif
#ifndef DEEP_DOG_TOUCH_RELEASE_ABS_MIN_1
#define DEEP_DOG_TOUCH_RELEASE_ABS_MIN_1 2200u
#endif
#ifndef DEEP_DOG_TOUCH_RELEASE_ABS_MIN_2
#define DEEP_DOG_TOUCH_RELEASE_ABS_MIN_2 650u
#endif
#ifndef DEEP_DOG_TOUCH_RELEASE_ABS_MIN_3
#define DEEP_DOG_TOUCH_RELEASE_ABS_MIN_3 400u
#endif
#ifndef DEEP_DOG_TOUCH_BASELINE_ALPHA
#define DEEP_DOG_TOUCH_BASELINE_ALPHA 0.01f
#endif
#ifndef DEEP_DOG_TOUCH_BASELINE_UPDATE_ABS_RATIO
#define DEEP_DOG_TOUCH_BASELINE_UPDATE_ABS_RATIO 0.12f
#endif
#ifndef DEEP_DOG_TOUCH_BASELINE_UPDATE_ABS_OFFSET
#define DEEP_DOG_TOUCH_BASELINE_UPDATE_ABS_OFFSET 200u
#endif

/** 标定：空闲采噪声时长 / 总超时 / 默认次数 */
#ifndef DEEP_DOG_TOUCH_CALIB_IDLE_MS
#define DEEP_DOG_TOUCH_CALIB_IDLE_MS 500
#endif
#ifndef DEEP_DOG_TOUCH_CALIB_TIMEOUT_MS
#define DEEP_DOG_TOUCH_CALIB_TIMEOUT_MS 60000
#endif
#ifndef DEEP_DOG_TOUCH_CALIB_DEFAULT_SAMPLES
#define DEEP_DOG_TOUCH_CALIB_DEFAULT_SAMPLES 10
#endif
#ifndef DEEP_DOG_TOUCH_CALIB_MAX_SAMPLES
#define DEEP_DOG_TOUCH_CALIB_MAX_SAMPLES 20
#endif
/** 软阈下限；idle 太干净时避免把噪声当按下 */
#ifndef DEEP_DOG_TOUCH_CALIB_SOFT_FLOOR
#define DEEP_DOG_TOUCH_CALIB_SOFT_FLOOR 150u
#endif
/** 有效峰至少达到 soft_thr 的倍数，过滤松手噪声 */
#ifndef DEEP_DOG_TOUCH_CALIB_PEAK_SOFT_MULT
#define DEEP_DOG_TOUCH_CALIB_PEAK_SOFT_MULT 4u
#endif
/** 有效峰绝对下限（本机键3 实按约 1500+） */
#ifndef DEEP_DOG_TOUCH_CALIB_PEAK_ABS_MIN
#define DEEP_DOG_TOUCH_CALIB_PEAK_ABS_MIN 400u
#endif
/** 记一次有效峰后的冷却（ms），避免松手抖动连记 */
#ifndef DEEP_DOG_TOUCH_CALIB_REFRACTORY_MS
#define DEEP_DOG_TOUCH_CALIB_REFRACTORY_MS 300
#endif
/** 标定结果 press_abs_min 下限 */
#ifndef DEEP_DOG_TOUCH_CALIB_RESULT_FLOOR
#define DEEP_DOG_TOUCH_CALIB_RESULT_FLOOR 200u
#endif

/** NVS namespace for TouchThresholds blob */
#ifndef DEEP_DOG_TOUCH_THR_NVS_NS
#define DEEP_DOG_TOUCH_THR_NVS_NS "touch_thr"
#endif
#ifndef DEEP_DOG_TOUCH_THR_NVS_KEY
#define DEEP_DOG_TOUCH_THR_NVS_KEY "thr"
#endif
#ifndef DEEP_DOG_TOUCH_THR_NVS_VER
#define DEEP_DOG_TOUCH_THR_NVS_VER 1
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
