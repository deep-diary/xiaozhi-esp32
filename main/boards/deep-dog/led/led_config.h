#ifndef _DEEP_DOG_LED_CONFIG_H_
#define _DEEP_DOG_LED_CONFIG_H_

#include "config.h"

/**
 * 灯带模块配置。
 * EXT_PIN_MODE=LED 时 DIN 默认 gpio_a(38)、count 默认 24。
 * MQTT / 控制层 mode：0 关 / 1 静态 / 2 闪烁 / 3 呼吸 / 4 滚动 / 5 系统(应用绑定)。
 * （旧 SparkBot UART light_mode_t 已废弃，勿再使用。）
 */

/** MQTT / 控制层灯效 mode */
typedef enum {
    DEEP_DOG_LED_MODE_OFF = 0,
    DEEP_DOG_LED_MODE_STATIC = 1,
    DEEP_DOG_LED_MODE_BLINK = 2,
    DEEP_DOG_LED_MODE_BREATHE = 3,
    DEEP_DOG_LED_MODE_SCROLL = 4,
    DEEP_DOG_LED_MODE_SYSTEM = 5,
} deep_dog_led_mode_t;

#endif  // _DEEP_DOG_LED_CONFIG_H_
