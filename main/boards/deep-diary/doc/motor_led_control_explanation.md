# 电机灯带控制类详细解释

## 核心设计思路

电机灯带控制类 `DeepMotorLedState` 的设计思路是将电机角度状态实时映射到24颗LED的环形灯带上，通过不同颜色的LED来直观显示电机的各种状态。

## 主要组件结构

### 1. 数据结构

#### MotorAngleState（电机角度状态）
```cpp
struct MotorAngleState {
    float current_angle;     // 当前角度（弧度）
    float target_angle;      // 目标角度（弧度）
    bool is_moving;          // 是否在移动
    bool is_error;           // 是否有错误
    AngleRange valid_range;  // 有效角度范围
};
```

#### AngleRange（角度范围）
```cpp
struct AngleRange {
    float min_angle;  // 最小角度（弧度）
    float max_angle;  // 最大角度（弧度）
    bool is_valid;    // 是否有效
};
```

### 2. 核心成员变量

```cpp
MotorAngleState motor_states_[MOTOR_COUNT];        // 6个电机的角度状态
AngleRange angle_ranges_[MOTOR_COUNT];             // 6个电机的角度范围
bool angle_indicators_enabled_[MOTOR_COUNT];       // 6个电机的角度指示器启用状态
CircularStrip* led_strip_;                         // LED灯带控制器
DeepMotor* deep_motor_;                           // 电机控制器
```

## 状态更新机制

### 1. 手动状态更新

#### SetMotorAngleState() - 单个电机状态更新
```cpp
void DeepMotorLedState::SetMotorAngleState(uint8_t motor_id, const MotorAngleState& state) {
    // 1. 参数验证
    if (motor_id < 1 || motor_id > MOTOR_COUNT) return;
    
    // 2. 保存旧状态
    int index = motor_id - 1;
    MotorAngleState old_state = motor_states_[index];
    
    // 3. 更新新状态
    motor_states_[index] = state;
    
    // 4. 检查状态变化
    if (old_state.current_angle != state.current_angle || 
        old_state.is_moving != state.is_moving || 
        old_state.is_error != state.is_error) {
        OnStateChanged(motor_id, state);  // 触发状态变化处理
    }
}
```

#### UpdateAllMotorStates() - 批量电机状态更新
```cpp
void DeepMotorLedState::UpdateAllMotorStates(const MotorAngleState states[6]) {
    bool any_changed = false;
    
    // 1. 批量更新所有电机状态
    for (int i = 0; i < MOTOR_COUNT; i++) {
        MotorAngleState old_state = motor_states_[i];
        motor_states_[i] = states[i];
        
        // 2. 检查是否有任何变化
        if (old_state.current_angle != states[i].current_angle || 
            old_state.is_moving != states[i].is_moving || 
            old_state.is_error != states[i].is_error) {
            any_changed = true;
        }
    }
    
    // 3. 如果有变化，更新LED显示
    if (any_changed) {
        UpdateLedDisplay();
    }
}
```

### 2. 状态变化处理

#### OnStateChanged() - 状态变化回调
```cpp
void DeepMotorLedState::OnStateChanged(uint8_t motor_id, const MotorAngleState& new_state) {
    // 1. 记录状态变化日志
    ESP_LOGD(TAG, "电机%d状态变化: 角度%.2f rad, 移动:%s, 错误:%s", 
             motor_id, new_state.current_angle, 
             new_state.is_moving ? "是" : "否",
             new_state.is_error ? "是" : "否");
    
    // 2. 立即更新LED显示
    UpdateLedDisplay();
    
    // 3. 调用用户回调函数（如果设置了）
    if (state_change_callback_) {
        state_change_callback_(new_state, current_effect_);
    }
}
```

## LED显示更新机制

### 1. UpdateLedDisplay() - LED显示更新核心

```cpp
void DeepMotorLedState::UpdateLedDisplay() {
    // 1. 检查LED灯带是否可用
    if (!led_strip_) return;
    
    // 2. 检查是否有启用的角度指示器
    bool has_enabled_indicators = false;
    for (int i = 0; i < MOTOR_COUNT; i++) {
        if (angle_indicators_enabled_[i]) {
            has_enabled_indicators = true;
            break;
        }
    }
    
    // 3. 如果没有启用的指示器，关闭所有LED
    if (!has_enabled_indicators) {
        StopCurrentEffect();
        return;
    }
    
    // 4. 关闭所有LED（准备重新绘制）
    led_strip_->SetAllColor({0, 0, 0});
    
    // 5. 为每个启用的电机应用角度指示器效果
    for (int i = 0; i < MOTOR_COUNT; i++) {
        if (angle_indicators_enabled_[i]) {
            const MotorAngleState& state = motor_states_[i];
            
            if (state.is_error) {
                // 错误状态用红色闪烁（优先级最高）
                ApplyErrorIndicatorEffect(i + 1);
                break;  // 错误状态优先级最高，只显示错误
            } else {
                // 正常状态显示角度
                ApplyAngleIndicatorEffect(i + 1);
            }
        }
    }
}
```

### 2. ApplyAngleIndicatorEffect() - 角度指示器效果

```cpp
void DeepMotorLedState::ApplyAngleIndicatorEffect(uint8_t motor_id) {
    // 1. 获取电机状态
    int index = motor_id - 1;
    const MotorAngleState& state = motor_states_[index];
    
    // 2. 计算当前角度对应的LED索引
    int led_index = AngleToLedIndex(state.current_angle);
    
    // 3. 设置当前角度位置的LED为蓝色
    uint8_t blue_color[3] = {0, 0, 255};
    led_strip_->SetPixelColor(led_index, blue_color);
    
    // 4. 如果电机正在移动，目标角度用青色表示
    if (state.is_moving) {
        int target_led_index = AngleToLedIndex(state.target_angle);
        uint8_t cyan_color[3] = {0, 255, 255};
        led_strip_->SetPixelColor(target_led_index, cyan_color);
    }
    
    // 5. 如果有有效角度范围，用绿色表示范围边界
    if (angle_ranges_[index].is_valid) {
        int min_led_index = AngleToLedIndex(angle_ranges_[index].min_angle);
        int max_led_index = AngleToLedIndex(angle_ranges_[index].max_angle);
        uint8_t green_color[3] = {0, 255, 0};
        
        led_strip_->SetPixelColor(min_led_index, green_color);
        led_strip_->SetPixelColor(max_led_index, green_color);
    }
}
```

## 角度到LED映射算法

### AngleToLedIndex() - 角度转换算法

```cpp
int DeepMotorLedState::AngleToLedIndex(float angle) const {
    // 1. 将角度标准化到 [0, 2π) 范围
    while (angle < 0) angle += TWO_PI;
    while (angle >= TWO_PI) angle -= TWO_PI;
    
    // 2. 将角度映射到LED索引 (0-23)
    // 0度对应第0个LED，180度对应第12个LED
    int led_index = static_cast<int>((angle / TWO_PI) * LED_COUNT);
    
    // 3. 确保索引在有效范围内
    if (led_index >= LED_COUNT) led_index = LED_COUNT - 1;
    if (led_index < 0) led_index = 0;
    
    return led_index;
}
```

**映射关系：**
- 0度 → LED 0
- 90度 → LED 6
- 180度 → LED 12
- 270度 → LED 18
- 360度 → LED 0（回到起点）

## 颜色编码系统

### 1. 颜色定义
- **蓝色 (0,0,255)**：当前角度位置
- **青色 (0,255,255)**：目标角度位置（电机移动时）
- **绿色 (0,255,0)**：有效角度范围边界
- **红色 (255,0,0)**：错误状态（闪烁）

### 2. 显示优先级
1. **错误状态**：红色闪烁（最高优先级）
2. **正常状态**：蓝色当前角度 + 青色目标角度 + 绿色范围边界

## 启用/禁用机制

### EnableAngleIndicator() - 启用/禁用角度指示器

```cpp
void DeepMotorLedState::EnableAngleIndicator(uint8_t motor_id, bool enabled) {
    // 1. 参数验证
    if (motor_id < 1 || motor_id > MOTOR_COUNT) return;
    
    // 2. 设置启用状态
    int index = motor_id - 1;
    angle_indicators_enabled_[index] = enabled;
    
    // 3. 根据启用状态决定操作
    if (enabled) {
        UpdateLedDisplay();  // 启用时立即更新显示
    } else {
        StopCurrentEffect(); // 禁用时停止效果
    }
}
```

## 回答您的问题

### 启用功能后，一旦电机状态变化，就会更新LED显示吗？

**答案：是的，但需要手动触发状态更新。**

### 具体工作流程：

1. **启用角度指示器**
   ```cpp
   motor_led_state_->EnableAngleIndicator(motor_id, true);
   ```

2. **状态更新触发**
   ```cpp
   // 方式1：单个电机状态更新
   MotorAngleState new_state = {current_angle, target_angle, is_moving, is_error, valid_range};
   motor_led_state_->SetMotorAngleState(motor_id, new_state);
   
   // 方式2：批量电机状态更新
   MotorAngleState all_states[6] = {...};
   motor_led_state_->UpdateAllMotorStates(all_states);
   ```

3. **自动LED更新**
   - 状态变化检测 → `OnStateChanged()` → `UpdateLedDisplay()` → LED显示更新

### 关键点：

1. **不是自动的**：需要外部代码主动调用状态更新方法
2. **变化检测**：只有状态真正发生变化时才会更新LED
3. **立即更新**：一旦检测到变化，立即更新LED显示
4. **优先级处理**：错误状态优先级最高，会覆盖其他显示

### 建议的集成方式：

```cpp
// 在电机状态查询任务中
void motor_status_task(void* parameter) {
    while (1) {
        // 1. 获取电机状态
        motor_status_t status;
        if (deep_motor_->getMotorStatus(motor_id, &status)) {
            // 2. 转换为LED状态
            MotorAngleState led_state = {
                status.current_angle,
                status.target_angle,
                status.is_moving,
                status.has_error,
                {min_angle, max_angle, true}
            };
            
            // 3. 更新LED显示
            motor_led_state_->SetMotorAngleState(motor_id, led_state);
        }
        
        vTaskDelay(pdMS_TO_TICKS(50)); // 50ms更新一次
    }
}
```

这样就能实现电机状态变化时自动更新LED显示的效果。
