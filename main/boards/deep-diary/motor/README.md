# 电机控制模块 (Motor Control Module)

## 功能概述

本模块提供了对深度电机(DeepMotor)的完整控制功能，包括：
- 底层CAN通信协议
- 电机状态管理
- 多电机协调控制
- LED状态反馈
- MCP服务器接口

## 文件说明

### 核心文件

- **protocol_motor.cpp/h**: 电机底层通信协议实现
  - CAN帧格式定义
  - 电机指令编码/解码
  - 电机反馈数据解析

- **deep_motor.cpp/h**: 电机管理器
  - 多电机注册与管理
  - 电机状态查询
  - 电机控制指令发送
  - 集成LED状态显示

- **deep_motor_control.cc/h**: MCP控制接口
  - 暴露MCP工具函数
  - 提供用户友好的控制接口
  - 支持位置/速度/力矩控制模式

- **deep_motor_led_state.cc/h**: 电机LED状态管理
  - 根据电机状态自动更新LED颜色
  - 支持多种状态显示（运动中、故障、空闲等）

## 主要功能

### 1. 电机注册
```cpp
// 注册单个电机
deep_motor->registerMotor(motor_id);

// 批量注册
uint8_t motor_ids[] = {1, 2, 3, 4, 5, 6};
for (int i = 0; i < 6; i++) {
    deep_motor->registerMotor(motor_ids[i]);
}
```

### 2. 电机控制
```cpp
// 使能/失能电机
deep_motor->enable(motor_id);
deep_motor->disable(motor_id);

// 位置控制
deep_motor->setTargetPosition(motor_id, angle, speed);

// 速度控制
deep_motor->setTargetSpeed(motor_id, speed);
```

### 3. 状态查询
```cpp
// 获取电机当前角度
float angle = deep_motor->getCurrentAngle(motor_id);

// 获取电机状态
MotorState state = deep_motor->getMotorState(motor_id);

// 检查是否在线
bool online = deep_motor->isMotorOnline(motor_id);
```

### 4. MCP工具使用
通过MCP服务器可以使用以下工具：
- `motor_enable`: 使能电机
- `motor_disable`: 失能电机
- `motor_set_position`: 设置目标位置
- `motor_set_speed`: 设置目标速度
- `motor_get_status`: 获取电机状态

## 使用示例

### 基本控制流程
```cpp
// 1. 创建电机管理器
DeepMotor* deep_motor = new DeepMotor(led_strip);

// 2. 注册电机
deep_motor->registerMotor(1);

// 3. 使能电机
deep_motor->enable(1);

// 4. 发送控制指令
deep_motor->setTargetPosition(1, 90.0f, 10.0f);  // 移动到90度，速度10度/秒

// 5. 查询状态
float current_angle = deep_motor->getCurrentAngle(1);
```

### 创建MCP控制接口
```cpp
auto& mcp_server = McpServer::GetInstance();
DeepMotorControl* motor_control = new DeepMotorControl(deep_motor, mcp_server);
```

## 依赖关系

- **CAN模块**: 用于电机通信
- **LED模块**: 用于状态显示（可选）
- **MCP服务器**: 用于对外提供控制接口

## 注意事项

1. 使用前确保CAN总线已正确初始化
2. 电机ID范围：1-255
3. 电机状态更新依赖于CAN接收任务
4. LED状态显示需要先初始化WS2812灯带
5. 使能电机前请确保机械结构安全

## 相关文档

详细的使用说明和示例请参考：
- `../doc/motor_led_usage.md` - 电机LED使用说明
- `../doc/motor_zero_test_guide.md` - 电机零位测试指南
- `../doc/protocol_motor_example.md` - 协议使用示例

