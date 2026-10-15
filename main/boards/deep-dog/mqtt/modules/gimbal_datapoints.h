#pragma once

/**
 * gimbal 模块数据点声明（Phase 3：单实例标量）。
 *
 * 镜像 status/gimbal 的标量字段（pan/tilt/速度/限位等）到数据点体系。
 * 现有 DeepDogGimbalMqtt::PublishStatus 继续发布 status/gimbal（向后兼容）。
 *
 * 文档：docs/interface/04-datapoint-registry.md
 */

#include "mqtt/datapoint_registry.h"

/** 注册 gimbal 模块数据点到全局 Registry（由 gimbal MQTT 模块的 SetControl/构造调用） */
void GimbalDataPoints_Register();

const DataModuleDef* GimbalDataPoints_GetModuleDef();

constexpr size_t kGimbalDataPointCount = 12;
