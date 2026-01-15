# 电机角度LED指示器使用指南

## 概述

电机角度LED指示器是一个新的组件，可以将电机实际角度映射到24颗LED的环形灯带上，用不同颜色的灯珠表示当前角度位置、目标角度和有效角度范围。

## 功能特点

### 1. 角度映射
- **24颗LED环形灯带**：每颗LED代表15度（360°/24）
- **0度对应第1个LED**：顺时针排列
- **180度对应第13个LED**：半圈位置
- **360度对应第1个LED**：完整一圈

### 2. 颜色编码
- **蓝色**：当前角度位置
- **青色**：目标角度位置（电机移动时显示）
- **绿色**：有效角度范围边界
- **红色闪烁**：错误状态

### 3. 支持功能
- 单个电机角度指示
- 角度范围设置
- 多电机同时显示
- 实时状态更新

## MCP工具列表

### 1. 启用角度指示器

#### 工具信息
- **工具名称**: `self.motor.enable_angle_indicator`
- **功能**: 启用指定电机的角度LED指示器
- **参数**:
  - `motor_id`: 电机ID (1-255)

#### 使用示例
```python
# 启用电机1的角度指示器
self.motor.enable_angle_indicator(motor_id=1)

# 启用电机3的角度指示器
self.motor.enable_angle_indicator(motor_id=3)
```

### 2. 禁用角度指示器

#### 工具信息
- **工具名称**: `self.motor.disable_angle_indicator`
- **功能**: 禁用指定电机的角度LED指示器
- **参数**:
  - `motor_id`: 电机ID (1-255)

#### 使用示例
```python
# 禁用电机1的角度指示器
self.motor.disable_angle_indicator(motor_id=1)
```

### 3. 设置角度范围

#### 工具信息
- **工具名称**: `self.motor.set_angle_range`
- **功能**: 设置电机的有效角度范围
- **参数**:
  - `motor_id`: 电机ID (1-255)
  - `min_angle`: 最小角度 (-314到314, 单位0.01弧度)
  - `max_angle`: 最大角度 (-314到314, 单位0.01弧度)

#### 使用示例
```python
# 设置电机1的角度范围为-π到π
self.motor.set_angle_range(motor_id=1, min_angle=-314, max_angle=314)

# 设置电机2的角度范围为0到π/2
self.motor.set_angle_range(motor_id=2, min_angle=0, max_angle=157)
```

#### 参数说明
- **角度单位**: 输入值除以100得到实际弧度
  - 输入314 → 实际3.14弧度 (π)
  - 输入157 → 实际1.57弧度 (π/2)
  - 输入-314 → 实际-3.14弧度 (-π)

### 4. 获取角度状态

#### 工具信息
- **工具名称**: `self.motor.get_angle_status`
- **功能**: 获取指定电机的角度状态信息
- **参数**:
  - `motor_id`: 电机ID (1-255)

#### 使用示例
```python
# 获取电机1的角度状态
self.motor.get_angle_status(motor_id=1)
```

#### 返回信息
- 当前角度（弧度）
- 目标角度（弧度）
- 是否在移动
- 是否有错误
- 角度范围
- 范围是否有效
- 指示器是否启用

### 5. 停止所有指示器

#### 工具信息
- **工具名称**: `self.motor.stop_all_indicators`
- **功能**: 停止所有电机的角度指示器
- **参数**: 无

#### 使用示例
```python
# 停止所有角度指示器
self.motor.stop_all_indicators()
```

## 使用流程

### 基本使用流程
1. **设置角度范围**（可选）
   ```python
   self.motor.set_angle_range(motor_id=1, min_angle=-314, max_angle=314)
   ```

2. **启用角度指示器**
   ```python
   self.motor.enable_angle_indicator(motor_id=1)
   ```

3. **查看角度状态**
   ```python
   self.motor.get_angle_status(motor_id=1)
   ```

4. **停止指示器**（可选）
   ```python
   self.motor.disable_angle_indicator(motor_id=1)
   ```

### 多电机使用示例
```python
# 设置多个电机的角度范围
for motor_id in range(1, 7):
    self.motor.set_angle_range(motor_id=motor_id, min_angle=-314, max_angle=314)

# 启用多个电机的角度指示器
for motor_id in range(1, 7):
    self.motor.enable_angle_indicator(motor_id=motor_id)

# 查看多个电机的状态
for motor_id in range(1, 7):
    self.motor.get_angle_status(motor_id=motor_id)
```

## 应用场景

### 1. 电机调试
- 实时查看电机角度位置
- 验证角度范围设置
- 监控电机运动状态

### 2. 机械臂标定
- 可视化关节角度
- 验证边界设置
- 监控标定过程

### 3. 教学演示
- 直观显示角度概念
- 演示电机运动
- 可视化角度范围

### 4. 故障诊断
- 快速识别角度异常
- 监控电机状态
- 错误状态指示

## 注意事项

1. **LED数量限制**: 当前支持24颗LED，每颗LED代表15度
2. **角度范围**: 支持-π到π的完整角度范围
3. **实时更新**: 角度状态需要定期更新才能实时显示
4. **多电机显示**: 可以同时显示多个电机的角度，但会叠加显示
5. **错误优先级**: 错误状态会覆盖其他显示效果

## 技术细节

### 角度转换公式
```
LED索引 = (角度 / 2π) × 24
角度 = (LED索引 / 24) × 2π
```

### 颜色定义
- 蓝色: RGB(0, 0, 255) - 当前角度
- 青色: RGB(0, 255, 255) - 目标角度
- 绿色: RGB(0, 255, 0) - 角度范围
- 红色: RGB(255, 0, 0) - 错误状态

### 状态更新
- 角度状态通过 `SetMotorAngleState()` 方法更新
- LED显示通过 `UpdateLedDisplay()` 方法刷新
- 支持状态变化回调函数
