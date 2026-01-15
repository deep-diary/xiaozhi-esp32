# 电机LED自动启用功能使用说明

## 功能概述

电机LED角度指示器现在会在电机初始化时自动启用，无需手动配置。这大大简化了使用流程，让LED显示功能更加便捷。

## 自动启用机制

### 1. 初始化时自动启用
当创建 `DeepMotor` 实例时，会自动启用所有电机的角度指示器：

```cpp
// 创建电机管理器时自动启用LED功能
DeepMotor* motor = new DeepMotor(led_strip);
// 此时电机ID 1-6 的角度指示器已自动启用
```

**日志输出：**
```
I (xxx) DeepMotor: LED状态管理器初始化完成
I (xxx) DeepMotor: 已自动启用所有电机角度指示器（电机ID: 1-6）
```

### 2. 新电机注册时自动启用
当通过CAN总线检测到新电机时，会自动启用该电机的角度指示器：

```cpp
// 当收到新电机的CAN帧时
// 系统会自动注册电机并启用角度指示器
```

**日志输出：**
```
I (xxx) DeepMotor: 新注册电机ID: 3，当前已注册 3 个电机
I (xxx) DeepMotor: 自动启用新注册电机3的角度指示器
```

## 使用流程

### 最简单的使用方式
```cpp
// 1. 创建电机管理器（LED功能自动启用）
DeepMotor* motor = new DeepMotor(led_strip);

// 2. 启动电机状态查询任务
motor->startInitStatusTask(motor_id);

// 3. 完成！LED会自动显示电机角度
```

### MCP工具使用
```bash
# 启动电机状态查询任务（LED已自动启用）
self.motor.start_status_task(motor_id=1)

# 设置角度范围（可选）
self.motor.set_angle_range(motor_id=1, min_angle=-314, max_angle=314)

# 查看角度状态
self.motor.get_angle_status(motor_id=1)
```

## 自动启用的电机ID范围

- **初始化时**：自动启用电机ID 1-6 的角度指示器
- **运行时**：新检测到的任何电机ID都会自动启用角度指示器

## LED显示效果

启用角度指示器后，LED会显示：

- **蓝色LED**：当前电机角度位置
- **青色LED**：目标角度位置（电机移动时）
- **绿色LED**：有效角度范围边界（如果设置了范围）
- **红色LED**：错误状态（闪烁）

## 手动控制（可选）

虽然角度指示器已自动启用，但您仍然可以手动控制：

```bash
# 禁用特定电机的角度指示器
self.motor.disable_angle_indicator(motor_id=1)

# 重新启用特定电机的角度指示器
self.motor.enable_angle_indicator(motor_id=1)

# 停止所有角度指示器
self.motor.stop_all_indicators()
```

## 优势

1. **零配置**：无需手动启用角度指示器
2. **自动适应**：新电机自动启用LED显示
3. **即时反馈**：电机状态变化时LED立即响应
4. **简化流程**：只需启动状态查询任务即可

## 注意事项

1. **LED灯带必需**：需要提供有效的 `CircularStrip` 实例
2. **电机注册**：只有注册的电机才会显示LED状态
3. **状态查询**：需要启动状态查询任务才能获取实时数据
4. **资源消耗**：所有电机同时启用LED会增加一些计算开销

## 故障排除

### LED不显示
1. 检查LED灯带是否正确初始化
2. 确认电机是否已注册（查看日志）
3. 检查状态查询任务是否启动

### 部分电机LED不显示
1. 确认电机是否通过CAN总线通信
2. 检查电机ID是否在1-6范围内
3. 查看是否有错误状态（红色LED）

这种自动启用机制让电机LED显示功能变得更加智能和用户友好！
