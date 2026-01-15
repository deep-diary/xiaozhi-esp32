# 机械臂MCP工具使用指南

## 概述

机械臂MCP工具提供了完整的6自由度机械臂控制功能，包括基本控制、录制播放、边界标定和状态查询。

## 工具分类

### 1. 基本控制工具

#### 设置机械臂零位
- **工具名称**: `self.arm.set_zero_position`
- **功能**: 同时设置6个电机的零位
- **参数**: 无
- **使用示例**:
```python
self.arm.set_zero_position()
```

#### 启动机械臂
- **工具名称**: `self.arm.enable`
- **功能**: 同时使能6个电机
- **参数**: 无
- **使用示例**:
```python
self.arm.enable()
```

#### 关闭机械臂
- **工具名称**: `self.arm.disable`
- **功能**: 同时关闭6个电机
- **参数**: 无
- **使用示例**:
```python
self.arm.disable()
```

#### 初始化机械臂
- **工具名称**: `self.arm.initialize`
- **功能**: 初始化机械臂，设置位置模式和最大速度
- **参数**: 
  - `max_speed`: 最大速度 (10-300, 单位0.1 rad/s)
- **使用示例**:
```python
self.arm.initialize(max_speed=300)  # 30.0 rad/s
```

### 2. 位置控制工具

#### 设置机械臂位置
- **工具名称**: `self.arm.set_position`
- **功能**: 设置6个关节的位置（带边界检查）
- **参数**: 
  - `pos1`: 关节1位置 (-314 到 314, 单位0.01 rad)
  - `pos2`: 关节2位置 (-314 到 314, 单位0.01 rad)
  - `pos3`: 关节3位置 (-314 到 314, 单位0.01 rad)
  - `pos4`: 关节4位置 (-314 到 314, 单位0.01 rad)
  - `pos5`: 关节5位置 (-314 到 314, 单位0.01 rad)
  - `pos6`: 关节6位置 (-314 到 314, 单位0.01 rad)
- **使用示例**:
```python
# 设置所有关节到零位
self.arm.set_position(pos1=0, pos2=0, pos3=0, pos4=0, pos5=0, pos6=0)

# 设置特定位置
self.arm.set_position(pos1=157, pos2=-100, pos3=50, pos4=0, pos5=0, pos6=0)
# 对应: [1.57, -1.00, 0.50, 0.00, 0.00, 0.00] rad
```

### 3. 录制功能工具

#### 开始录制
- **工具名称**: `self.arm.start_recording`
- **功能**: 开始录制机械臂动作
- **参数**: 无
- **使用示例**:
```python
self.arm.start_recording()
# 然后手动拖动机械臂，系统会自动记录位置
```

#### 停止录制
- **工具名称**: `self.arm.stop_recording`
- **功能**: 停止录制并保存数据
- **参数**: 无
- **使用示例**:
```python
self.arm.stop_recording()
```

#### 播放录制
- **工具名称**: `self.arm.play_recording`
- **功能**: 播放录制的动作序列
- **参数**: 无
- **使用示例**:
```python
self.arm.play_recording()
```

#### 获取录制状态
- **工具名称**: `self.arm.get_recording_status`
- **功能**: 获取录制状态信息
- **参数**: 无
- **使用示例**:
```python
self.arm.get_recording_status()
```

### 4. 状态查询工具

#### 启动状态查询任务
- **工具名称**: `self.arm.start_status_task`
- **功能**: 启动1秒间隔的状态查询任务
- **参数**: 无
- **使用示例**:
```python
self.arm.start_status_task()
```

#### 停止状态查询任务
- **工具名称**: `self.arm.stop_status_task`
- **功能**: 停止状态查询任务
- **参数**: 无
- **使用示例**:
```python
self.arm.stop_status_task()
```

#### 获取机械臂状态
- **工具名称**: `self.arm.get_status`
- **功能**: 获取机械臂完整状态信息
- **参数**: 无
- **使用示例**:
```python
self.arm.get_status()
```

#### 打印机械臂状态
- **工具名称**: `self.arm.print_status`
- **功能**: 打印机械臂状态到日志
- **参数**: 无
- **使用示例**:
```python
self.arm.print_status()
```

### 5. 边界标定工具

#### 开始边界标定
- **工具名称**: `self.arm.start_boundary_calibration`
- **功能**: 开始边界位置标定
- **参数**: 无
- **使用示例**:
```python
self.arm.start_boundary_calibration()
# 然后依次转动每个关节到最大最小位置
```

#### 停止边界标定
- **工具名称**: `self.arm.stop_boundary_calibration`
- **功能**: 停止边界标定并保存数据
- **参数**: 无
- **使用示例**:
```python
self.arm.stop_boundary_calibration()
```

#### 获取边界数据
- **工具名称**: `self.arm.get_boundary`
- **功能**: 获取边界标定数据
- **参数**: 无
- **使用示例**:
```python
self.arm.get_boundary()
```

#### 检查边界是否已标定
- **工具名称**: `self.arm.is_boundary_calibrated`
- **功能**: 检查边界是否已标定
- **参数**: 无
- **使用示例**:
```python
self.arm.is_boundary_calibrated()
```

## 典型使用流程

### 1. 首次使用流程
```python
# 1. 初始化机械臂
self.arm.initialize(max_speed=300)

# 2. 设置零位
self.arm.set_zero_position()

# 3. 进行边界标定
self.arm.start_boundary_calibration()
# 手动转动各关节到极限位置
self.arm.stop_boundary_calibration()

# 4. 启动机械臂
self.arm.enable()

# 5. 测试位置控制
self.arm.set_position(pos1=0, pos2=0, pos3=0, pos4=0, pos5=0, pos6=0)
```

### 2. 录制播放流程
```python
# 1. 开始录制
self.arm.start_recording()

# 2. 手动拖动机械臂完成动作
# 系统自动记录位置数据

# 3. 停止录制
self.arm.stop_recording()

# 4. 播放录制
self.arm.play_recording()
```

### 3. 状态监控流程
```python
# 1. 启动状态查询
self.arm.start_status_task()

# 2. 获取状态信息
self.arm.get_status()

# 3. 停止状态查询
self.arm.stop_status_task()
```

## 注意事项

1. **边界标定**: 首次使用前必须进行边界标定
2. **位置控制**: 未标定边界时无法进行位置控制
3. **录制功能**: 录制前确保机械臂已停止
4. **任务管理**: 使用完毕后记得停止相关任务
5. **参数范围**: 注意各参数的有效范围

## 错误处理

- 所有工具都会返回成功/失败消息
- 失败时会记录详细错误日志
- 建议在调用前检查机械臂状态

## 语音控制建议

- **"设置机械臂零位"** → `self.arm.set_zero_position()`
- **"启动机械臂"** → `self.arm.enable()`
- **"关闭机械臂"** → `self.arm.disable()`
- **"开始录制"** → `self.arm.start_recording()`
- **"停止录制"** → `self.arm.stop_recording()`
- **"播放录制"** → `self.arm.play_recording()`
- **"开始边界标定"** → `self.arm.start_boundary_calibration()`
- **"停止边界标定"** → `self.arm.stop_boundary_calibration()`
