#ifndef _DEEP_DOG_GIMBAL_CONFIG_H_
#define _DEEP_DOG_GIMBAL_CONFIG_H_

#include "servo/servo_config.h"

/** 点按步进角（度） */
#ifndef DEEP_DOG_GIMBAL_STEP_DEG
#define DEEP_DOG_GIMBAL_STEP_DEG 5
#endif

/** 默认轴速度（度/秒） */
#ifndef DEEP_DOG_GIMBAL_DEFAULT_PAN_SPEED
#define DEEP_DOG_GIMBAL_DEFAULT_PAN_SPEED 40
#endif
#ifndef DEEP_DOG_GIMBAL_DEFAULT_TILT_SPEED
#define DEEP_DOG_GIMBAL_DEFAULT_TILT_SPEED 30
#endif

/** 速度档范围与步进（度/秒） */
#ifndef DEEP_DOG_GIMBAL_SPEED_MIN
#define DEEP_DOG_GIMBAL_SPEED_MIN 5
#endif
#ifndef DEEP_DOG_GIMBAL_SPEED_MAX
#define DEEP_DOG_GIMBAL_SPEED_MAX 120
#endif
#ifndef DEEP_DOG_GIMBAL_SPEED_STEP
#define DEEP_DOG_GIMBAL_SPEED_STEP 5
#endif

/** Jog 周期（ms）；每 tick 推进 speed * period / 1000 度 */
#ifndef DEEP_DOG_GIMBAL_JOG_PERIOD_MS
#define DEEP_DOG_GIMBAL_JOG_PERIOD_MS 40
#endif

#endif  // _DEEP_DOG_GIMBAL_CONFIG_H_
