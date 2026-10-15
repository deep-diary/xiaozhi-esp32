#pragma once

/**
 * handle 模块数据点声明（Phase 3）。
 *
 * 手柄的 axes/buttons 是嵌套复合结构，按数据点体系约束（不支持复合类型）
 * 扁平化为标量点：handle.axes_lx、handle.btn_a 等。
 * 现有 PublishStatus 继续发布嵌套结构的 status/handle（向后兼容）。
 *
 * 文档：docs/interface/04-datapoint-registry.md
 */

#include "mqtt/datapoint_registry.h"

class HandleEventHub;

/** 注册 handle 模块数据点 */
void HandleDataPoints_Register(HandleEventHub* hub);

const DataModuleDef* HandleDataPoints_GetModuleDef();

constexpr size_t kHandleDataPointCount = 14;
