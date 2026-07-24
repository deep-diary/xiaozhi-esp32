#pragma once

#include <string>

/** 1=编译板级 MQTT（device/stream）；0=桩 */
#ifndef DEEP_DOG_MQTT_ENABLE
#define DEEP_DOG_MQTT_ENABLE 1
#endif

/** 1=暴露 track MQTT / capabilities.track；0=关闭 */
#ifndef DEEP_DOG_TRACK_MQTT_ENABLE
#define DEEP_DOG_TRACK_MQTT_ENABLE 1
#endif

#ifndef DEEP_DOG_MQTT_DEFAULT_BROKER_HOST
#define DEEP_DOG_MQTT_DEFAULT_BROKER_HOST "192.168.31.25"
#endif

#ifndef DEEP_DOG_MQTT_DEFAULT_BROKER_PORT
#define DEEP_DOG_MQTT_DEFAULT_BROKER_PORT 1883
#endif

/** Topic 前缀中的 device_id；与 RTSP path deep-dog/<id> 对齐 */
#ifndef DEEP_DOG_MQTT_DEFAULT_DEVICE_ID
#define DEEP_DOG_MQTT_DEFAULT_DEVICE_ID "dev"
#endif

#ifndef DEEP_DOG_MQTT_DEFAULT_KEEPALIVE_S
#define DEEP_DOG_MQTT_DEFAULT_KEEPALIVE_S 60
#endif

/** device/status 心跳周期（µs）；0.2 Hz → 5s */
#ifndef DEEP_DOG_MQTT_STATUS_INTERVAL_US
#define DEEP_DOG_MQTT_STATUS_INTERVAL_US (5 * 1000 * 1000)
#endif

/** stream/status 轮询周期（µs），用于 on_change 检测 */
#ifndef DEEP_DOG_MQTT_STREAM_POLL_INTERVAL_US
#define DEEP_DOG_MQTT_STREAM_POLL_INTERVAL_US (1000 * 1000)
#endif

/** face/status 轮询周期（µs），on_change_or_poll ≈ 2 Hz */
#ifndef DEEP_DOG_MQTT_FACE_POLL_INTERVAL_US
#define DEEP_DOG_MQTT_FACE_POLL_INTERVAL_US (500 * 1000)
#endif

/** track/status 轮询周期（µs），on_change ≈ 2 Hz */
#ifndef DEEP_DOG_MQTT_TRACK_POLL_INTERVAL_US
#define DEEP_DOG_MQTT_TRACK_POLL_INTERVAL_US (500 * 1000)
#endif

/** imu/status 发布周期见 sensor/imu_config.h（DEEP_DOG_MQTT_IMU_INTERVAL_US） */

struct DeepDogMqttSettings {
    std::string broker_host = DEEP_DOG_MQTT_DEFAULT_BROKER_HOST;
    int broker_port = DEEP_DOG_MQTT_DEFAULT_BROKER_PORT;
    std::string device_id = DEEP_DOG_MQTT_DEFAULT_DEVICE_ID;
    std::string client_id;  // 空则按 MAC 生成
    std::string username;
    std::string password;
    int keepalive_s = DEEP_DOG_MQTT_DEFAULT_KEEPALIVE_S;
};

class DeepDogMqttConfig {
public:
    static DeepDogMqttSettings Load();
    static void Save(const DeepDogMqttSettings& s);
    static std::string TopicPrefix(const std::string& device_id);
    static std::string DefaultClientId(const std::string& device_id);
};
