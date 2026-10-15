#pragma once

#include <string>

/**
 * 配置中心顶层配置结构（设备端视角）。
 *
 * 字段命名严格对齐后端种子模板（deep-trace
 * devices/management/commands/seed_default_config_templates.py）：
 *   connectivity.mqtt.{broker_host,broker_port,use_tls,username,password,topic_root,keepalive_s,qos_default}
 *   connectivity.mediamtx.{rtsp_host,rtsp_port,hls_host,hls_port,stream_path_template,push_protocol}
 *   connectivity.api_server.{base_url,timeout_ms,tls_verify}
 *
 * 完整 JSON 存 NVS（deep_dog_cfg.config_json），解析后各模块按需读取。
 */

namespace config_center {

struct MqttConfig {
    std::string broker_host;
    int broker_port = 1883;
    bool use_tls = false;
    std::string username;
    std::string password;
    std::string topic_root = "deep-trace";
    int keepalive_s = 60;
    int qos_default = 1;
};

struct MediamtxConfig {
    std::string rtsp_host;
    int rtsp_port = 8554;
    std::string hls_host;
    int hls_port = 8888;
    /** 流路径模板，含 ${device_id}，如 "deep-dog/${device_id}" */
    std::string stream_path_template = "deep-dog/${device_id}";
    std::string push_protocol = "rtsp";
};

struct ApiServerConfig {
    std::string base_url;
    int timeout_ms = 15000;
    bool tls_verify = true;
};

/** 设备端本地同步策略（后端模板暂未定义，缺省即用编译默认） */
struct ConfigCenterConfig {
    int pull_interval_s = 300;
    bool enable_mqtt_notify = true;
};

struct DeviceConfig {
    std::string schema_version = "device.config.v1";
    int config_version = 0;
    std::string fingerprint;  ///< SHA256 hex (64 chars)
    std::string issued_at;    ///< ISO8601
    int ttl_s = 300;

    MqttConfig mqtt;
    MediamtxConfig mediamtx;
    ApiServerConfig api_server;
    ConfigCenterConfig config_center;
};

}  // namespace config_center
