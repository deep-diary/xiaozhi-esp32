#pragma once

#include <string>

/**
 * 统一 MQTT topic 构造（跨端契约 01-mqtt-contract）。
 *
 * 骨架：{prefix}/{device_id}/{kind}/{subject}[/...]
 *   - prefix 由 DeepDogMqttConfig::TopicPrefix() 提供（可配置，默认 deep-trace）
 *   - kind ∈ info | status | cmd | event | result
 *   - 命令应答为 cmd/{subject}/ack
 *
 * 各模块 Publish/Subscribe 传入的相对 topic 一律经此处构造，避免散落硬编码。
 */
namespace deep_dog_topics {

inline std::string Info(const char* subject) { return std::string("info/") + subject; }
inline std::string Status(const char* subject) { return std::string("status/") + subject; }
inline std::string Cmd(const char* subject) { return std::string("cmd/") + subject; }
inline std::string Event(const char* subject) { return std::string("event/") + subject; }
inline std::string Result(const char* subject) { return std::string("result/") + subject; }

/** 命令应答 topic（相对）：cmd/{subject}/ack */
inline std::string Ack(const std::string& subject) { return "cmd/" + subject + "/ack"; }

/** 设备命令通配订阅（单条覆盖全部模块命令） */
inline const char* CmdWildcard() { return "cmd/#"; }

}  // namespace deep_dog_topics
