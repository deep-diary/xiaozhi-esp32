#pragma once

/**
 * can 模块数据点声明（Phase 4）。
 *
 * 镜像 status/can 的标量字段（总线状态、错误计数、隧道开关等）。
 * 通过 RegisterDataPoints() 由 DeepDogCanMqtt 内部注册（需访问其私有时序状态）。
 *
 * 现有 DeepDogCanMqtt::PublishStatus 继续发布 status/can（向后兼容）。
 *
 * 文档：docs/interface/04-datapoint-registry.md
 */

#include "mqtt/datapoint_registry.h"

#include <cstdint>

/** 注册 can 模块数据点（传入模块内部状态指针；`tunnel`/`allow_tx` 为可写点） */
void CanDataPoints_Register(bool* tunnel, bool* allow_tx, const uint32_t* dropped);

const DataModuleDef* CanDataPoints_GetModuleDef();

constexpr size_t kCanDataPointCount = 11;
