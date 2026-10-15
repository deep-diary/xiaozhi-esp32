#pragma once

/**
 * LED 模块数据点声明。
 *
 * 遵循 interop-04 Data Point Registry 规范。
 * 这些数据点同时暴露到 MQTT / HTTP / MCP / Telemetry 四通道。
 *
 * 文档：docs/interface/04-datapoint-registry.md
 */

#include "mqtt/datapoint_registry.h"

class LedStripControl;

/** LED 模块显示名 */
inline const char* kLedModuleDisplayName = "LED 灯带";
inline const char* kLedModuleDescription = "RGB LED 灯带控制，支持静态/呼吸/闪烁/滚动/系统模式";

/**
 * 构建 LED 模块的 DataPointDef 数组（静态）。
 * 调用方负责注册到 DataPointRegistry。
 *
 * @param ctrl  LED 控制器指针（用作回调上下文）
 * @param defs  输出：DataPointDef 数组指针（静态存储，生命周期等于进程）
 * @param count 输出：数组大小
 */
void LedDataPoints_GetDefs(LedStripControl* ctrl,
                           const DataPointDef** defs,
                           size_t* count);

/** 获取模块定义（含显示名、描述） */
const DataModuleDef* LedDataPoints_GetModuleDef();
