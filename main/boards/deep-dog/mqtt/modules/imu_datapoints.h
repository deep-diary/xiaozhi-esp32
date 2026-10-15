#pragma once

/**
 * imu 模块数据点声明（Phase 4：高频发布验证）。
 *
 * 镜像 status/imu 的姿态/加速度/角速度标量。IMU 读取有一定开销，
 * 内部用短时缓存避免同一发布周期内重复读取。
 *
 * 现有 DeepDogImuMqtt::PublishStatus 继续发布 status/imu（含 switches 计数，向后兼容）。
 *
 * 文档：docs/interface/04-datapoint-registry.md
 */

#include "mqtt/datapoint_registry.h"

/** 注册 imu 模块数据点（sensor/hub 以 void* 传入以规避条件类型） */
void ImuDataPoints_Register(void* sensor, void* hub);

const DataModuleDef* ImuDataPoints_GetModuleDef();

constexpr size_t kImuDataPointCount = 10;
