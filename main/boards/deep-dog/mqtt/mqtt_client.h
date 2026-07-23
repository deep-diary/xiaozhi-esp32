#pragma once

#include "mqtt/mqtt_config.h"

#include <functional>
#include <memory>
#include <string>

/**
 * 板级 MQTT 传输层（esp_mqtt_client）。
 * 支持 retain；与小智语音 MqttProtocol 独立（独立 client handle）。
 */
class DeepDogMqttClient {
public:
    using MessageCallback = std::function<void(const std::string& topic, const std::string& payload)>;
    using ConnectionCallback = std::function<void(bool connected)>;

    DeepDogMqttClient();
    ~DeepDogMqttClient();

    DeepDogMqttClient(const DeepDogMqttClient&) = delete;
    DeepDogMqttClient& operator=(const DeepDogMqttClient&) = delete;

    bool Start(const DeepDogMqttSettings& settings);
    void Stop();
    bool IsConnected() const;

    bool Publish(const std::string& relative_topic, const std::string& payload, int qos, bool retain);
    bool Subscribe(const std::string& relative_topic, int qos);
    bool Unsubscribe(const std::string& relative_topic);

    std::string Topic(const std::string& relative) const;
    const DeepDogMqttSettings& settings() const { return settings_; }
    const std::string& prefix() const { return prefix_; }

    void SetMessageCallback(MessageCallback cb) { message_cb_ = std::move(cb); }
    void SetConnectionCallback(ConnectionCallback cb) { connection_cb_ = std::move(cb); }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    DeepDogMqttSettings settings_;
    std::string prefix_;
    MessageCallback message_cb_;
    ConnectionCallback connection_cb_;
};
