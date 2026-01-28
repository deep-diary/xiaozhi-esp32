# 传感器模块 (Sensor Module) - Deep Thumble

本目录用于存放 Deep Thumble 板子的各类传感器驱动与封装，目前规划支持：

- BMI270 六轴 IMU（加速度计 + 陀螺仪）✅（接口已预留，占位实现）

## 目录结构

```text
sensor/
├── README.md           # 本文件
├── imu_sensor.h        # IMU 传感器 C++ 封装（BMI270 抽象）
└── imu_sensor.cc       # IMU 传感器实现（当前为占位实现）
```

## ImuSensor 简介

`ImuSensor` 提供一个面向 C++ 的 BMI270 访问接口，主要职责：

- 持有 I2C 总线句柄（与音频 Codec / 其他 I2C 外设共用）
- 提供 `Initialize()` 用于完成 BMI270 初始化
- 提供 `ReadRawData()` 用于采集一次 IMU 原始数据

当前实现为占位版本，仅完成基本框架和日志输出，后续可接入真实 BMI270 驱动（参考 SWRS 中提到的 `ref/factory_demo_v1/main/app/app_imu.c`）。

## 集成规划

根据 SWRS：

- **采集周期**：10ms（在用户主循环的 10ms 周期任务中调用）
- **初始化位置**：在 `DeepThumble` 构造函数中完成传感器初始化
- **后续扩展**：
  - 数据融合（姿态角计算）
  - 事件检测（跌倒 / 振动等）
  - 与用户主循环框架（阶段三）对接

当前阶段仅完成：

1. 传感器模块文件结构搭建
2. `ImuSensor` 类接口设计与占位实现
3. 在板级代码中预留初始化入口（稍后在阶段三的用户主循环中接入 10ms 采集调度）

