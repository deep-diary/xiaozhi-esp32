# 电机协议层使用示例

## 概述

新的电机协议层 (`protocol_motor.h` 和 `protocol_motor.cpp`) 提供了统一的接口来控制小米电机，支持多电机控制。

## 主要特性

- **多电机支持**: 通过电机ID参数支持控制多个电机
- **统一接口**: 提供简洁的API接口，隐藏CAN帧构建细节
- **多种控制模式**: 支持位置、速度、电流和运控模式
- **错误处理**: 完善的错误处理和状态反馈

## 基本使用

### 1. 系统初始化

```cpp
#include "protocol_motor.h"
#include <ESP32-TWAI-CAN.hpp>

// CAN总线初始化（在系统初始化时调用）
ESP32Can.setPins(CAN_TX, CAN_RX);
ESP32Can.setRxQueueSize(50);
ESP32Can.setTxQueueSize(50);
ESP32Can.setSpeed(ESP32Can.convertSpeed(1000)); // 1Mbps

if (ESP32Can.begin()) {
    ESP_LOGI(TAG, "CAN总线初始化成功");
} else {
    ESP_LOGE(TAG, "CAN总线初始化失败");
}
```

### 2. 电机控制

```cpp
// 使能电机 (ID=1)
MotorProtocol::enableMotor(1);

// 设置位置模式 (ID=1, 位置=1.5弧度, 最大速度=10rad/s)
MotorProtocol::setPositionMode(1, 1.5f, 10.0f);

// 设置速度模式 (ID=1, 速度=2.0rad/s)
MotorProtocol::setSpeedMode(1, 2.0f);

// 运控模式 (ID=1, 扭矩=1.0Nm, 位置=0.5rad, 速度=1.0rad/s, Kp=30, Kd=0.5)
MotorProtocol::controlMotor(1, 1.0f, 0.5f, 1.0f, 30.0f, 0.5f);

// 停止电机
MotorProtocol::resetMotor(1);
```

### 3. 读取电机状态

```cpp
motor_status_t status;
if (MotorProtocol::readMotorStatus(1, &status, 100)) {
    Serial.printf("电机ID: %d, 角度: %.2f, 速度: %.2f, 扭矩: %.2f, 温度: %.1f°C\n",
                  status.motor_id, status.current_angle, status.current_speed,
                  status.current_torque, status.current_temp);
}
```

## MCP工具接口

通过MCP服务器，可以使用以下工具控制电机：

### 位置控制
```json
{
  "tool": "self.can.send_motor_position",
  "parameters": {
    "motor_id": 1,
    "position": 200  // -314 到 314 (实际值/100)
  }
}
```

### 速度控制
```json
{
  "tool": "self.can.send_motor_speed", 
  "parameters": {
    "motor_id": 1,
    "speed": 20  // -300 到 300 (实际值/10)
  }
}
```

### 电机使能
```json
{
  "tool": "self.can.enable_motor",
  "parameters": {
    "motor_id": 1
  }
}
```

### 电机停止
```json
{
  "tool": "self.can.reset_motor",
  "parameters": {
    "motor_id": 1
  }
}
```

### 运控模式
```json
{
  "tool": "self.can.control_motor",
  "parameters": {
    "motor_id": 1,
    "torque": 10,     // -120 到 120 (实际值/10)
    "position": 50,   // -125 到 125 (实际值/10)
    "speed": 30,      // -300 到 300 (实际值/10)
    "kp": 30,         // 0 到 500
    "kd": 5           // 0 到 50 (实际值/10)
  }
}
```

## 架构优势

1. **分离关注点**: CAN帧构建逻辑与业务逻辑分离，协议层只负责协议处理
2. **无依赖**: 不依赖Arduino库，使用ESP-IDF标准库，更加轻量
3. **职责单一**: CAN驱动初始化由上层负责，协议层只处理协议相关功能
4. **易于维护**: 统一的协议层便于修改和扩展
5. **多电机支持**: 通过ID参数轻松控制多个电机
6. **类型安全**: 使用枚举和结构体提高代码可读性
7. **错误处理**: 完善的错误处理和日志记录

## 注意事项

- 电机ID范围: 1-255
- 位置范围: -12.5 到 +12.5 弧度
- 速度范围: -30.0 到 +30.0 rad/s
- 扭矩范围: -12.0 到 +12.0 N·m
- 电流范围: -23.0 到 +23.0 A
