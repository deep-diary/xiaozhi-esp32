#pragma once

/**
 * device 模块数据点声明（Phase 2：核心身份 + 连接状态）。
 *
 * 涵盖 info/device 的核心身份字段 + status/device 的连接/资源字段。
 * 完整 device 模块仍由 DeepDogDeviceMqtt 管理（info/status 双主题，向后兼容），
 * 这里把标量字段镜像到 DataPointRegistry，走统一三通道。
 *
 * 文档：docs/interface/04-datapoint-registry.md
 */

#include "mqtt/datapoint_registry.h"

class DeepDogMqttClient;

/**
 * 注册 device 模块数据点到全局 Registry。
 * 由 DeepDogDeviceMqtt 构造函数调用（device 总是存在）。
 *
 * @param client  MQTT 客户端指针（用于读取 device_id 等上下文）
 */
void DeviceDataPoints_Register(DeepDogMqttClient* client);

/** 获取 device 模块定义（用于测试/调试） */
const DataModuleDef* DeviceDataPoints_GetModuleDef();

/** 数据点总数 */
constexpr size_t kDeviceDataPointCount = 13;
