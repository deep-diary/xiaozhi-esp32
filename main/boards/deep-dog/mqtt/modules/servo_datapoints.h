#pragma once

/**
 * servo 模块数据点声明（Phase 3：子实例机制验证）。
 *
 * 舵机是"多个同构实体"，每个舵机有 angle/target/attached/... 等标量。
 * 通过 RegisterMultiInstanceModule 展开为 servo.<index>.<point>：
 *   servo.0.angle / servo.1.angle ...
 * 实例号由 sub_id 提供（0-based），回调从 ctx 取实例号。
 *
 * 现有 DeepDogServoMqtt::PublishStatus 继续发布 status/servo（数组格式，向后兼容）。
 *
 * 文档：docs/interface/04-datapoint-registry.md
 */

#include "mqtt/datapoint_registry.h"

/** 注册 servo 模块数据点（按 DeepDogServoCount() 展开子实例） */
void ServoDataPoints_Register();

const DataModuleDef* ServoDataPoints_GetModuleDef();

/** 每个舵机的模板点数量 */
constexpr size_t kServoPointPerInstance = 7;
