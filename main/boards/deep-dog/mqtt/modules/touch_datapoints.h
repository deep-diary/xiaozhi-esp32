#pragma once

/**
 * touch 模块数据点声明（Phase 3）。
 *
 * 镜像 status/touch 的核心标量（ok / pressed_mask / 各按钮按下态）。
 * 3 个触摸按钮的完整状态数组属于复合结构，仍由现有 PublishStatus 发布。
 *
 * 文档：docs/interface/04-datapoint-registry.md
 */

#include "mqtt/datapoint_registry.h"

class TouchEventHub;
class TouchButtonController;

/** 注册 touch 模块数据点（传入 hub/ctrl 以便读取实时状态） */
void TouchDataPoints_Register(TouchEventHub* hub, TouchButtonController* ctrl);

const DataModuleDef* TouchDataPoints_GetModuleDef();

constexpr size_t kTouchDataPointCount = 5;
