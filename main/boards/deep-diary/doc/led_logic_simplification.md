# LED逻辑简化方案

## 问题背景

用户反馈：灯带只属于一个电机，当前的逻辑过于复杂，需要管理6个电机的状态，导致LED覆盖问题。

## 简化方案

### 核心思路
**只显示当前活跃电机的角度**，而不是管理所有6个电机的状态。

### 简化前后对比

#### 简化前的复杂逻辑
```cpp
// 需要检查所有6个电机的状态
for (int i = 0; i < MOTOR_COUNT; i++) {
    if (angle_indicators_enabled_[i] && !breathe_effects_enabled_[i]) {
        // 复杂的条件判断
        if (state.current_angle != 0.0f || state.is_moving) {
            ApplyAngleIndicatorEffect(i + 1);
        }
    }
}
```

#### 简化后的简洁逻辑
```cpp
// 只关注当前活跃电机
uint8_t active_motor_id = deep_motor_->getActiveMotorId();
if (active_motor_id == 0) {
    led_strip_->SetAllColor({0, 0, 0});
    return;
}

// 只处理活跃电机的状态
const MotorAngleState& state = motor_states_[active_motor_id - 1];
if (state.is_error) {
    ApplyErrorIndicatorEffect(active_motor_id);
} else {
    ApplyAngleIndicatorEffect(active_motor_id);
}
```

## 简化的优势

### 1. 逻辑更清晰
- **单一职责**：只关注当前活跃电机
- **减少条件判断**：不需要检查6个电机的状态
- **避免LED覆盖**：只有一个电机会设置LED

### 2. 性能更好
- **减少循环**：不需要遍历6个电机
- **减少计算**：只计算一个电机的角度到LED映射
- **减少内存访问**：只访问活跃电机的数据

### 3. 维护更容易
- **代码更简洁**：逻辑路径更少
- **调试更简单**：只需要关注一个电机
- **扩展更容易**：如果需要支持多电机，可以基于此逻辑扩展

## 实现细节

### 1. 保持接口兼容性
- 保持原有的公共接口不变
- 内部实现简化为只关注活跃电机
- 不影响现有的MCP工具调用

### 2. 活跃电机获取
```cpp
uint8_t active_motor_id = deep_motor_->getActiveMotorId();
```
- 通过DeepMotor获取当前活跃电机ID
- 如果没有活跃电机，关闭所有LED

### 3. 状态检查简化
```cpp
// 只检查活跃电机的状态
if (!angle_indicators_enabled_[motor_index]) {
    led_strip_->SetAllColor({0, 0, 0});
    return;
}

if (breathe_effects_enabled_[motor_index]) {
    return; // 呼吸灯模式，跳过角度显示
}
```

## 预期效果

### 1. 用户体验
- **LED响应更快**：不需要等待所有电机状态检查
- **显示更准确**：只显示当前操作的电机角度
- **无LED覆盖**：不会出现多个电机设置同一个LED的问题

### 2. 系统性能
- **CPU使用率降低**：减少不必要的计算
- **内存访问优化**：只访问需要的数据
- **响应时间缩短**：LED更新更快

### 3. 代码质量
- **可读性提升**：逻辑更清晰易懂
- **维护性提升**：代码更简洁
- **扩展性提升**：为未来功能扩展奠定基础

## 测试验证

修复后应该看到：
1. **只有活跃电机的角度会显示在LED上**
2. **LED位置正确跟随电机角度变化**
3. **没有LED闪烁或覆盖问题**
4. **呼吸灯模式正常工作**

## 总结

这个简化方案既解决了当前的LED覆盖问题，又为未来的功能扩展提供了更好的基础。通过只关注当前活跃电机，我们大大简化了逻辑，提高了性能和可维护性。
