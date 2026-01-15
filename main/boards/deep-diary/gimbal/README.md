# 云台控制模块 (Gimbal Control Module)

## 功能概述

本模块提供了双轴云台的控制功能，包括：
- 水平轴（Pan）控制：0-270度
- 垂直轴（Tilt）控制：0-180度
- MCP服务器接口
- CAN总线控制支持

## 文件说明

### 核心文件

- **Gimbal.c/h**: 云台底层控制
  - PWM舵机控制
  - 角度范围限制
  - CAN指令处理

- **gimbal_control.cc/h**: MCP控制接口
  - 暴露MCP工具函数
  - 提供用户友好的控制接口

## 主要功能

### 1. 云台初始化
```cpp
Gimbal_t gimbal;

// 初始化云台 - GPIO19(水平), GPIO20(垂直)
if (Gimbal_init(&gimbal, SERVO_PAN_GPIO, SERVO_TILT_GPIO) != ESP_OK) {
    ESP_LOGE(TAG, "云台初始化失败!");
    return;
}

// 设置初始位置
Gimbal_setAngles(&gimbal, 135, 90);  // 水平135度，垂直90度（中间位置）
```

### 2. 角度控制
```cpp
// 设置水平和垂直角度
Gimbal_setAngles(&gimbal, pan_angle, tilt_angle);

// 单独设置水平角度（0-270度）
Gimbal_setPan(&gimbal, pan_angle);

// 单独设置垂直角度（0-180度）
Gimbal_setTilt(&gimbal, tilt_angle);
```

### 3. CAN控制
```cpp
// 通过CAN总线控制云台
// CAN ID: CAN_CMD_SERVO_CONTROL
// 数据格式：[水平角度高字节, 水平角度低字节, 垂直角度高字节, 垂直角度低字节, ...]

CanFrame frame;
Gimbal_handleCanCommand(&gimbal, &frame);
```

### 4. MCP工具使用
通过MCP服务器可以使用以下工具：
- `gimbal_set_angle`: 设置云台角度
- `gimbal_set_pan`: 设置水平角度
- `gimbal_set_tilt`: 设置垂直角度
- `gimbal_reset`: 重置到中间位置

## 使用示例

### 基本控制流程
```cpp
// 1. 初始化云台
Gimbal_t gimbal;
Gimbal_init(&gimbal, 19, 20);  // GPIO19(水平), GPIO20(垂直)

// 2. 设置到中间位置
Gimbal_setAngles(&gimbal, 135, 90);

// 3. 控制云台移动
Gimbal_setAngles(&gimbal, 180, 120);  // 向右上移动

// 4. 清理资源
Gimbal_deinit(&gimbal);
```

### 创建MCP控制接口
```cpp
auto& mcp_server = McpServer::GetInstance();
GimbalControl* gimbal_control = new GimbalControl(&gimbal, mcp_server);
```

### 扫描模式示例
```cpp
// 水平扫描
for (int pan = 0; pan <= 270; pan += 10) {
    Gimbal_setPan(&gimbal, pan);
    vTaskDelay(pdMS_TO_TICKS(100));
}

// 垂直扫描
for (int tilt = 0; tilt <= 180; tilt += 10) {
    Gimbal_setTilt(&gimbal, tilt);
    vTaskDelay(pdMS_TO_TICKS(100));
}
```

## 舵机参数

### 硬件规格
- **水平舵机（Pan）**:
  - 角度范围：0-270度
  - 中间位置：135度
  - PWM频率：50Hz
  - 脉宽范围：500-2500μs

- **垂直舵机（Tilt）**:
  - 角度范围：0-180度
  - 中间位置：90度
  - PWM频率：50Hz
  - 脉宽范围：500-2500μs

### GPIO配置
默认配置（可在config.h中修改）：
```cpp
#define SERVO_PAN_GPIO  19  // 水平舵机
#define SERVO_TILT_GPIO 20  // 垂直舵机
```

## 依赖关系

- **Servo模块**: 底层舵机PWM控制
- **CAN模块**: CAN总线控制（可选）
- **MCP服务器**: 对外控制接口

## 注意事项

1. 角度范围限制：
   - 水平轴：0-270度
   - 垂直轴：0-180度
2. 超出范围的角度会被自动限制在有效范围内
3. 快速连续发送控制指令可能导致舵机抖动，建议添加适当延时
4. 使用前请确保舵机供电充足（5V，至少2A）
5. 首次使用建议从中间位置开始测试

## 相关文档

本模块相对独立，详细使用说明请参考代码注释。

