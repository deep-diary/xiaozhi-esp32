#pragma once

#include <string>

class DeepDogMqttClient;

/**
 * 统一配置中心 MQTT 通道（CFG-01）。
 *
 * 订阅 {prefix}/{device_id}/cmd/config：
 *   { "action": "refresh", "config_version": n?, "reason": "template_updated"? }
 * 收到后触发 ConfigManager 立即拉取（HTTP），并回 cmd/config/ack。
 * HTTP 定时轮询为兜底（见 ConfigManager）。
 */
class DeepDogConfigMqtt {
public:
    explicit DeepDogConfigMqtt(DeepDogMqttClient* client);

    void OnConnected();
    void OnDisconnected();
    void Stop();

    void OnMessage(const std::string& topic, const std::string& payload);

private:
    void PublishAck(const char* action, const char* result);

    DeepDogMqttClient* client_;
};
