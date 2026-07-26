#ifndef _DEEP_DOG_LED_CONFIG_H_
#define _DEEP_DOG_LED_CONFIG_H_

#include "config.h"

/**
 * 灯带模块配置（独立 GPIO，不占用 38/48）。
 * light_mode_t 保留自 SparkBot UART 灯效语义，便于后续对接；
 * WS2812 实装可参考 deep-diary/led（mode 0–5）。
 */

typedef enum {
    LIGHT_MODE_CHARGING_BREATH = 0,
    LIGHT_MODE_POWER_LOW,
    LIGHT_MODE_ALWAYS_ON,
    LIGHT_MODE_BLINK,
    LIGHT_MODE_WHITE_BREATH_SLOW,
    LIGHT_MODE_WHITE_BREATH_FAST,
    LIGHT_MODE_FLOWING,
    LIGHT_MODE_SHOW,
    LIGHT_MODE_SLEEP,
    LIGHT_MODE_MAX
} light_mode_t;

#endif  // _DEEP_DOG_LED_CONFIG_H_
