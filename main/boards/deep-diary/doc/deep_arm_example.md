# 机械臂控制组件使用示例

## 概述

`DeepArm` 类提供了6自由度机械臂的完整控制功能，包括基本控制、录制播放、边界标定和持久化存储。

## 基本使用

### 1. 创建机械臂控制器

```cpp
#include "deep_arm.h"

// 定义6个电机的ID
uint8_t motor_ids[6] = {1, 2, 3, 4, 5, 6};

// 创建机械臂控制器
DeepArm* arm = new DeepArm(deep_motor, motor_ids);
```

### 2. 基本控制功能

```cpp
// 初始化机械臂（设置位置模式，最大速度30 rad/s）
float max_speeds[6] = {30.0f, 30.0f, 30.0f, 30.0f, 30.0f, 30.0f};
arm->initializeArm(max_speeds);

// 设置零位
arm->setZeroPosition();

// 启动机械臂
arm->enableArm();

// 设置位置（带边界检查）
float positions[6] = {0.0f, 0.5f, -0.3f, 0.0f, 0.0f, 0.0f};
arm->setArmPosition(positions);

// 关闭机械臂
arm->disableArm();
```

### 3. 录制功能

```cpp
// 开始录制
arm->startRecording();
// 此时可以手动拖动机械臂，系统会自动记录位置

// 停止录制
arm->stopRecording();

// 播放录制
arm->playRecording();
```

### 4. 边界标定

```cpp
// 开始边界标定
arm->startBoundaryCalibration();
// 依次转动每个关节到最大最小位置

// 停止边界标定
arm->stopBoundaryCalibration();
// 边界数据会自动保存到NVS存储
```

### 5. 状态查询

```cpp
// 启动状态查询任务
arm->startStatusQueryTask();

// 获取机械臂状态
arm_status_t status;
arm->getArmStatus(&status);

// 获取边界数据
arm_boundary_t boundary;
arm->getArmBoundary(&boundary);

// 打印状态信息
arm->printArmStatus();
```

## 功能特性

### 1. 软件限位
- 边界标定后，所有位置控制都会进行边界检查
- 防止机械臂超出安全范围
- 边界数据持久化存储，断电不丢失

### 2. 录制播放
- 同时录制6个电机的动作
- 50ms采样间隔，精确记录
- 支持300个录制点

### 3. 任务管理
- 状态查询任务：1秒间隔
- 边界查询任务：0.5秒间隔
- 自动任务清理

### 4. 错误处理
- 完整的错误检查和日志记录
- 任务创建失败保护
- 边界检查保护

## 注意事项

1. **边界标定**：首次使用前必须进行边界标定
2. **电机注册**：确保所有6个电机都已注册到DeepMotor
3. **任务管理**：使用完毕后记得停止相关任务
4. **内存管理**：使用完毕后删除DeepArm对象

## 错误码说明

- `false`：操作失败，查看日志获取详细错误信息
- `true`：操作成功

## 日志级别

- `ESP_LOGI`：重要操作信息
- `ESP_LOGD`：调试信息（录制点、边界更新）
- `ESP_LOGW`：警告信息
- `ESP_LOGE`：错误信息
