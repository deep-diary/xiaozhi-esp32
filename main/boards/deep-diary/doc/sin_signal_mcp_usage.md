# 正弦信号MCP工具使用指南

## 概述

正弦信号MCP工具提供了电机正弦波信号控制功能，可以用于电机测试、振动测试、周期性运动等应用场景。

## 工具列表

### 1. 开始正弦信号

#### 工具信息
- **工具名称**: `self.motor.start_sin_signal`
- **功能**: 开始电机正弦信号输出
- **参数**:
  - `motor_id`: 电机ID (1-255)
  - `amplitude`: 幅度 (1-1000, 单位0.01，即0.01-10.00)
  - `frequency`: 频率 (1-100, 单位0.1，即0.1-10.0 Hz)

#### 使用示例
```python
# 开始电机1的正弦信号，幅度1.0，频率1.0 Hz
self.motor.start_sin_signal(motor_id=1, amplitude=100, frequency=10)

# 开始电机2的正弦信号，幅度2.5，频率0.5 Hz
self.motor.start_sin_signal(motor_id=2, amplitude=250, frequency=5)

# 开始电机3的正弦信号，幅度0.5，频率2.0 Hz
self.motor.start_sin_signal(motor_id=3, amplitude=50, frequency=20)
```

#### 参数说明
- **motor_id**: 目标电机的ID，范围1-255
- **amplitude**: 正弦波幅度，输入值除以100得到实际幅度
  - 输入100 → 实际幅度1.0
  - 输入250 → 实际幅度2.5
  - 输入50 → 实际幅度0.5
- **frequency**: 正弦波频率，输入值除以10得到实际频率(Hz)
  - 输入10 → 实际频率1.0 Hz
  - 输入5 → 实际频率0.5 Hz
  - 输入20 → 实际频率2.0 Hz

### 2. 停止正弦信号

#### 工具信息
- **工具名称**: `self.motor.stop_sin_signal`
- **功能**: 停止电机正弦信号输出
- **参数**:
  - `motor_id`: 电机ID (1-255)

#### 使用示例
```python
# 停止电机1的正弦信号
self.motor.stop_sin_signal(motor_id=1)

# 停止电机2的正弦信号
self.motor.stop_sin_signal(motor_id=2)
```

## 应用场景

### 1. 电机测试
```python
# 测试电机在不同频率下的响应
frequencies = [5, 10, 20, 50]  # 0.5, 1.0, 2.0, 5.0 Hz
amplitude = 100  # 1.0

for freq in frequencies:
    print(f"测试频率: {freq/10.0} Hz")
    self.motor.start_sin_signal(motor_id=1, amplitude=amplitude, frequency=freq)
    time.sleep(5)  # 运行5秒
    self.motor.stop_sin_signal(motor_id=1)
    time.sleep(1)  # 停止1秒
```

### 2. 振动测试
```python
# 低频振动测试
self.motor.start_sin_signal(motor_id=1, amplitude=50, frequency=2)  # 0.5幅度，0.2Hz
time.sleep(10)  # 振动10秒
self.motor.stop_sin_signal(motor_id=1)
```

### 3. 周期性运动
```python
# 周期性摆动
self.motor.start_sin_signal(motor_id=2, amplitude=200, frequency=15)  # 2.0幅度，1.5Hz
time.sleep(30)  # 摆动30秒
self.motor.stop_sin_signal(motor_id=2)
```

## 注意事项

1. **电机状态**: 确保电机已正确初始化和使能
2. **参数范围**: 
   - 幅度范围: 0.01-10.00
   - 频率范围: 0.1-10.0 Hz
3. **安全考虑**: 高频大幅度信号可能对机械结构造成冲击，请谨慎使用
4. **停止信号**: 使用完毕后务必调用停止信号工具
5. **多电机控制**: 可以同时对多个电机发送不同的正弦信号

## 错误处理

工具会返回操作结果字符串：
- 成功: "开始/停止正弦信号成功 - 电机ID: X, 参数信息"
- 失败: "开始/停止正弦信号失败 - 电机ID: X"

请根据返回信息判断操作是否成功，并进行相应的错误处理。
