#pragma once

/** 1=编译 BMI270 + MQTT imu/status；0=无 IMU 代码 */
#ifndef DEEP_DOG_IMU_ENABLE
#define DEEP_DOG_IMU_ENABLE 1
#endif

/** MQTT imu/status 发布周期（µs）；YAML publish_hz≈10 → 100ms */
#ifndef DEEP_DOG_MQTT_IMU_INTERVAL_US
#define DEEP_DOG_MQTT_IMU_INTERVAL_US (100 * 1000)
#endif
