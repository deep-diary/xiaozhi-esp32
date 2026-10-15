#pragma once

/**
 * system 模块数据点声明（Phase 2：系统资源观测）。
 *
 * 涵盖内存使用、任务栈水位、复位计数等系统级可观测字段。
 * 这些字段之前散落在 device/status 的内存报告中，现在独立为 system 模块，
 * 便于数据点体系下的统一观测与告警。
 *
 * 文档：docs/interface/04-datapoint-registry.md
 */

#include "mqtt/datapoint_registry.h"

/**
 * 注册 system 模块数据点到全局 Registry。
 * 由 DeepDogMqtt 初始化时调用（总是存在）。
 */
void SystemDataPoints_Register();

/** 获取 system 模块定义（用于测试/调试） */
const DataModuleDef* SystemDataPoints_GetModuleDef();

/** 数据点总数 */
constexpr size_t kSystemDataPointCount = 8;
