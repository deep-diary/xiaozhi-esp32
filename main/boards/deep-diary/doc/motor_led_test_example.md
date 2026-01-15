# 电机角度LED指示器测试示例

## 测试场景

以下是一些测试示例，用于验证电机角度LED指示器的功能。

## 示例1：基本角度指示

### 测试步骤
1. 启用电机1的角度指示器
2. 设置角度范围为-π到π
3. 查看角度状态

### MCP命令
```python
# 启用电机1的角度指示器
self.motor.enable_angle_indicator(motor_id=1)

# 设置角度范围为-π到π
self.motor.set_angle_range(motor_id=1, min_angle=-314, max_angle=314)

# 查看角度状态
self.motor.get_angle_status(motor_id=1)
```

### 预期结果
- 电机1的角度指示器已启用
- 角度范围设置为[-3.14, 3.14]弧度
- LED灯带显示当前角度位置（蓝色）和角度范围边界（绿色）

## 示例2：多电机角度指示

### 测试步骤
1. 为所有电机设置角度范围
2. 启用多个电机的角度指示器
3. 查看所有电机的状态

### MCP命令
```python
# 设置多个电机的角度范围
for motor_id in range(1, 7):
    self.motor.set_angle_range(motor_id=motor_id, min_angle=-314, max_angle=314)

# 启用电机1、3、5的角度指示器
self.motor.enable_angle_indicator(motor_id=1)
self.motor.enable_angle_indicator(motor_id=3)
self.motor.enable_angle_indicator(motor_id=5)

# 查看多个电机的状态
for motor_id in range(1, 7):
    self.motor.get_angle_status(motor_id=motor_id)
```

### 预期结果
- 所有电机的角度范围已设置
- 电机1、3、5的角度指示器已启用
- LED灯带显示多个电机的角度位置

## 示例3：角度范围测试

### 测试步骤
1. 设置不同的角度范围
2. 启用角度指示器
3. 验证范围显示

### MCP命令
```python
# 设置电机1的角度范围为0到π/2
self.motor.set_angle_range(motor_id=1, min_angle=0, max_angle=157)

# 设置电机2的角度范围为-π/2到π/2
self.motor.set_angle_range(motor_id=2, min_angle=-157, max_angle=157)

# 启用两个电机的角度指示器
self.motor.enable_angle_indicator(motor_id=1)
self.motor.enable_angle_indicator(motor_id=2)

# 查看状态
self.motor.get_angle_status(motor_id=1)
self.motor.get_angle_status(motor_id=2)
```

### 预期结果
- 电机1的角度范围：[0, 1.57]弧度
- 电机2的角度范围：[-1.57, 1.57]弧度
- LED灯带显示不同的角度范围边界

## 示例4：错误状态测试

### 测试步骤
1. 启用角度指示器
2. 模拟错误状态
3. 验证错误显示

### MCP命令
```python
# 启用电机1的角度指示器
self.motor.enable_angle_indicator(motor_id=1)

# 设置角度范围
self.motor.set_angle_range(motor_id=1, min_angle=-314, max_angle=314)

# 查看状态（正常情况下）
self.motor.get_angle_status(motor_id=1)
```

### 预期结果
- 正常情况下显示蓝色角度指示
- 错误状态下显示红色闪烁

## 示例5：停止指示器测试

### 测试步骤
1. 启用多个角度指示器
2. 逐个禁用
3. 最后停止所有指示器

### MCP命令
```python
# 启用多个电机的角度指示器
self.motor.enable_angle_indicator(motor_id=1)
self.motor.enable_angle_indicator(motor_id=2)
self.motor.enable_angle_indicator(motor_id=3)

# 禁用电机2的角度指示器
self.motor.disable_angle_indicator(motor_id=2)

# 停止所有角度指示器
self.motor.stop_all_indicators()
```

### 预期结果
- 电机1、2、3的角度指示器已启用
- 电机2的角度指示器已禁用
- 所有角度指示器已停止

## 验证要点

### 1. LED显示验证
- **蓝色灯珠**: 显示当前角度位置
- **青色灯珠**: 显示目标角度位置（移动时）
- **绿色灯珠**: 显示角度范围边界
- **红色闪烁**: 显示错误状态

### 2. 角度映射验证
- **0度**: 第1个LED（索引0）
- **90度**: 第7个LED（索引6）
- **180度**: 第13个LED（索引12）
- **270度**: 第19个LED（索引18）

### 3. 功能验证
- 角度指示器启用/禁用
- 角度范围设置
- 状态查询
- 错误处理

## 故障排除

### 常见问题
1. **角度指示器未显示**
   - 检查是否已启用角度指示器
   - 验证电机ID是否正确
   - 确认LED灯带是否正常工作

2. **角度范围显示异常**
   - 检查角度范围设置是否正确
   - 验证角度单位转换
   - 确认范围是否有效

3. **状态查询失败**
   - 检查角度LED状态管理器是否初始化
   - 验证电机ID范围
   - 确认系统状态

### 调试建议
1. 使用日志查看详细状态信息
2. 逐步测试每个功能
3. 验证参数设置是否正确
4. 检查硬件连接状态
