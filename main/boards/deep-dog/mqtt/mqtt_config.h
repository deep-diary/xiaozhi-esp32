#pragma once

#include "config.h"

#include <string>

/** 1=编译板级 MQTT（device/stream）；0=桩；默认见 board_features.h */
#ifndef DEEP_DOG_MQTT_ENABLE
#define DEEP_DOG_MQTT_ENABLE 1
#endif

/** 1=暴露 track MQTT / capabilities.track；0=关闭 */
#ifndef DEEP_DOG_TRACK_MQTT_ENABLE
#define DEEP_DOG_TRACK_MQTT_ENABLE 1
#endif

#ifndef DEEP_DOG_MQTT_DEFAULT_BROKER_HOST
#define DEEP_DOG_MQTT_DEFAULT_BROKER_HOST "10.99.79.54"
#endif

#ifndef DEEP_DOG_MQTT_DEFAULT_BROKER_PORT
#define DEEP_DOG_MQTT_DEFAULT_BROKER_PORT 1883
#endif

/**
 * Topic 前缀中的 device_id；与 RTSP path deep-dog/<id> 对齐。
 * 未绑定默认 "dev"；已绑定默认 STA MAC 紧凑串（见 Load / EffectiveDeviceId）。
 * NVS 显式写入 device_id 可覆盖（仅联调）。
 */
#ifndef DEEP_DOG_MQTT_DEFAULT_DEVICE_ID
#define DEEP_DOG_MQTT_DEFAULT_DEVICE_ID "dev"
#endif

/**
 * Topic 前缀根（跨端契约 01-mqtt-contract）：{prefix}/{device_id}/{kind}/{subject}。
 * 可配置（NVS deep_dog_mqtt.topic_root / 配对下发）；改配置即整体换前缀。
 */
#ifndef DEEP_DOG_MQTT_DEFAULT_TOPIC_ROOT
#define DEEP_DOG_MQTT_DEFAULT_TOPIC_ROOT "deep-trace"
#endif

#ifndef DEEP_DOG_MQTT_DEFAULT_KEEPALIVE_S
#define DEEP_DOG_MQTT_DEFAULT_KEEPALIVE_S 60
#endif

/** 业务 Broker 是否走 TLS（默认关；TCP :1883 为设备常用路径） */
#ifndef DEEP_DOG_MQTT_DEFAULT_TLS
#define DEEP_DOG_MQTT_DEFAULT_TLS 0
#endif

/** 遥测 device_program（deep-trace telemetry payload 字段） */
#ifndef DEEP_DOG_TELEMETRY_DEVICE_PROGRAM
#define DEEP_DOG_TELEMETRY_DEVICE_PROGRAM "esp_sparkbot"
#endif

/** 遥测上报周期（µs）；未配置 telemetry_topic 时不发 */
#ifndef DEEP_DOG_TELEMETRY_INTERVAL_US
#define DEEP_DOG_TELEMETRY_INTERVAL_US (15 * 1000 * 1000)
#endif

/**
 * 编译默认 deep-trace 服务器 API 基址（HTTP 配对）；配网页/NVS 非空则覆盖。
 * 生产默认留空，由配网门户设置（见 swrs/net/N03）。
 */
#ifndef DEEP_DOG_MQTT_DEFAULT_SERVER_API_BASE
#define DEEP_DOG_MQTT_DEFAULT_SERVER_API_BASE ""
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

/** face/status 控制态心跳：每 N 次轮询强制发一帧（retain=false 时供晚订阅 UI 同步开关） */
#ifndef DEEP_DOG_MQTT_FACE_STATUS_HEARTBEAT_POLLS
#define DEEP_DOG_MQTT_FACE_STATUS_HEARTBEAT_POLLS 60  /* 500ms × 60 ≈ 30s */
#endif

/** track/status 轮询周期（µs），on_change ≈ 2 Hz */
#ifndef DEEP_DOG_MQTT_TRACK_POLL_INTERVAL_US
#define DEEP_DOG_MQTT_TRACK_POLL_INTERVAL_US (500 * 1000)
#endif

/** imu/status 发布周期见 sensor/imu_config.h（DEEP_DOG_MQTT_IMU_INTERVAL_US） */

struct DeepDogMqttSettings {
    std::string broker_host = DEEP_DOG_MQTT_DEFAULT_BROKER_HOST;
    int broker_port = DEEP_DOG_MQTT_DEFAULT_BROKER_PORT;
    /** true=MQTT over TLS(SSL)；设备常用 plain TCP :1883 */
    bool use_tls = DEEP_DOG_MQTT_DEFAULT_TLS != 0;
    /** topic 前缀根（默认 deep-trace；可配置） */
    std::string topic_root = DEEP_DOG_MQTT_DEFAULT_TOPIC_ROOT;
    std::string device_id = DEEP_DOG_MQTT_DEFAULT_DEVICE_ID;
    std::string client_id;  // 空则按 MAC 生成
    std::string username;
    std::string password;
    int keepalive_s = DEEP_DOG_MQTT_DEFAULT_KEEPALIVE_S;
    /** deep-trace 服务器 API 基址（HTTP 配对，见 swrs/mqtt/modules/16-http-pairing） */
    std::string server_api_base;
    /** deep-trace 遥测话题（云端按工位分配；空=关闭，见 swrs/mqtt/modules/15-telemetry） */
    std::string telemetry_topic;
    std::string home_code;
    std::string work_unit_code;
    std::string device_program = DEEP_DOG_TELEMETRY_DEVICE_PROGRAM;
};

class DeepDogMqttConfig {
public:
    static DeepDogMqttSettings Load();
    static void Save(const DeepDogMqttSettings& s);
    /** 仅落盘 HTTP 配对 200 响应的业务 Broker / 遥测配置（其余键不动） */
    static void SaveBoundEndpoint(const std::string& broker_host, int broker_port, bool use_tls,
                                  const std::string& telemetry_topic, const std::string& home_code,
                                  const std::string& work_unit_code);
    static std::string TopicPrefix(const std::string& device_id);
    /** 当前生效的前缀根（默认 deep-trace） */
    static std::string TopicRoot();
    static void SetTopicRoot(const std::string& root);
    static std::string DefaultClientId(const std::string& device_id);
    /** STA MAC 紧凑小写无冒号，如 aabbccddeeff */
    static std::string MacCompactDeviceId();
    /** NVS override 非空则用之；否则 bound→MAC，未绑定→dev */
    static std::string EffectiveDeviceId(bool bound, const std::string& nvs_override);
    static std::string StreamPathForDeviceId(const std::string& device_id);
    static std::string RtspPushUrlForDeviceId(const std::string& device_id);
    static std::string PublicHlsUrlForDeviceId(const std::string& device_id);
    static std::string LanHlsUrlForDeviceId(const std::string& device_id);
};
