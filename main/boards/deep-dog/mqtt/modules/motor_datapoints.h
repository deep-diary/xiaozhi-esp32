#pragma once

/**
 * motor 模块数据点声明（Phase 4：大量子实例验证）。
 *
 * 电机是"动态子实例"——实例列表由 CAN 扫描结果决定（getRegisteredMotorIds）。
 * 因此注册是**懒注册**：电机就绪后再展开为 motor.<can_id>.<point>。
 * 用户只迁核心可观测标量；完整字段仍由 DeepDogMotorMqtt::PublishStatus 发布。
 *
 * 文档：docs/interface/04-datapoint-registry.md
 */

#include "mqtt/datapoint_registry.h"

/**
 * 尝试注册 motor 数据点（按当前已注册电机展开子实例）。
 * @return true 表示注册成功（或已完成）；false 表示电机尚未就绪，可稍后重试。
 */
bool MotorDataPoints_Register();

const DataModuleDef* MotorDataPoints_GetModuleDef();

/** 每个电机的模板点数量 */
constexpr size_t kMotorPointPerInstance = 12;
