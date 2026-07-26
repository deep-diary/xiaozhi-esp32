#pragma once

#include "config.h"

/** 1=编译 BMI270 + MQTT imu/status；默认见 board_features.h */
#ifndef DEEP_DOG_IMU_ENABLE
#define DEEP_DOG_IMU_ENABLE 1
#endif

/** MQTT imu/status 发布周期（µs）；YAML publish_hz≈10 → 100ms */
#ifndef DEEP_DOG_MQTT_IMU_INTERVAL_US
#define DEEP_DOG_MQTT_IMU_INTERVAL_US (100 * 1000)
#endif

/** 本地采集 / 开关识别周期（µs）；目标 100 Hz → 10ms */
#ifndef DEEP_DOG_IMU_SAMPLE_INTERVAL_US
#define DEEP_DOG_IMU_SAMPLE_INTERVAL_US (10 * 1000)
#endif

/** 旋转边沿：短时积分角阈值（deg） */
#ifndef DEEP_DOG_IMU_ROT_THRESHOLD_DEG
#define DEEP_DOG_IMU_ROT_THRESHOLD_DEG 75.0f
#endif

/** 旋转冷却（ms） */
#ifndef DEEP_DOG_IMU_ROT_COOLDOWN_MS
#define DEEP_DOG_IMU_ROT_COOLDOWN_MS 400
#endif

/** 平移边沿：去重力线性加速度阈值（m/s²） */
#ifndef DEEP_DOG_IMU_TRANS_THRESHOLD_MPS2
#define DEEP_DOG_IMU_TRANS_THRESHOLD_MPS2 6.5f
#endif

/** 平移冷却（ms） */
#ifndef DEEP_DOG_IMU_TRANS_COOLDOWN_MS
#define DEEP_DOG_IMU_TRANS_COOLDOWN_MS 200
#endif

/** 旋转触发后抑制平移的窗口（ms），避免猛转连带误触平移 */
#ifndef DEEP_DOG_IMU_TRANS_SUPPRESS_AFTER_ROT_MS
#define DEEP_DOG_IMU_TRANS_SUPPRESS_AFTER_ROT_MS 300
#endif
