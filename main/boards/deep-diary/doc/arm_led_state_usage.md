# 机械臂灯带状态管理使用指南

## 概述

机械臂灯带状态管理系统将机械臂的各种状态与灯带效果进行绑定，通过不同的灯光效果直观地显示机械臂的当前状态。

## 功能特性

### 1. 状态-效果映射

| 机械臂状态 | 灯带效果 | 颜色 | 描述 |
|-----------|---------|------|------|
| 未初始化 | 红色慢闪 | 红色 | 1秒间隔闪烁，表示需要初始化 |
| 已初始化 | 橙色呼吸灯 | 橙色渐变 | 2秒呼吸周期，表示已就绪 |
| 已启动 | 绿色常亮 | 绿色 | 表示机械臂正在运行 |
| 录制中 | 蓝色快闪 | 蓝色 | 200ms快闪，表示正在录制 |
| 播放中 | 紫色跑马灯 | 紫色 | 3个LED跑马灯效果 |
| 移动中 | 青色呼吸灯 | 青色 | 500ms呼吸周期 |
| 错误状态 | 红色快闪 | 红色 | 100ms快闪，表示错误 |
| 边界标定中 | 黄色跑马灯 | 黄色 | 2个LED跑马灯效果 |
| 边界未标定 | 橙色慢闪 | 橙色 | 2秒慢闪 |
| 边界已标定 | 绿色常亮 | 绿色 | 表示边界已标定 |

### 2. 自动状态更新

系统会自动检测机械臂状态变化并更新灯带效果：

- **状态查询任务**：每500ms检查一次机械臂状态
- **操作触发更新**：每次机械臂操作后立即更新状态
- **实时响应**：状态变化时灯带效果立即切换

## 使用方法

### 1. 基本使用

```cpp
// 创建灯带状态管理器
CircularStrip* led_strip = new CircularStrip(WS2812_STRIP_GPIO, WS2812_LED_COUNT);
ArmLedState* arm_led_state = new ArmLedState(led_strip);

// 手动设置状态
arm_led_state->SetArmState(ArmLedState::ArmState::RECORDING);

// 从机械臂状态结构体更新
arm_status_t status;
// ... 获取机械臂状态 ...
arm_led_state->UpdateFromArmStatus(status);
```

### 2. 状态变化回调

```cpp
// 设置状态变化回调
arm_led_state->SetStateChangeCallback([](ArmLedState::ArmState state, const ArmLedState::LedEffectConfig& config) {
    ESP_LOGI("ArmLedState", "状态变化: %d, 效果: %d", static_cast<int>(state), static_cast<int>(config.effect_type));
});
```

### 3. 自定义效果配置

```cpp
// 创建自定义效果配置
ArmLedState::LedEffectConfig custom_config = {
    ArmLedState::LedEffect::BREATHE,  // 呼吸灯效果
    {255, 0, 255},                    // 主颜色（紫色）
    {128, 0, 128},                    // 次颜色（深紫色）
    1000,                             // 1秒间隔
    0,                                // 长度（呼吸灯不需要）
    false                             // 不自动停止
};

// 应用自定义效果
arm_led_state->ApplyLedEffect(custom_config);
```

## 集成到机械臂控制

### 1. 在DeepArmControl中集成

```cpp
class DeepArmControl {
private:
    DeepArm* deep_arm_;
    ArmLedState* arm_led_state_;

public:
    DeepArmControl(DeepArm* deep_arm, McpServer& mcp_server, CircularStrip* led_strip) 
        : deep_arm_(deep_arm) {
        arm_led_state_ = new ArmLedState(led_strip);
    }
    
    void UpdateArmStatus() {
        if (deep_arm_ && arm_led_state_) {
            arm_status_t status;
            if (deep_arm_->getArmStatus(&status)) {
                arm_led_state_->UpdateFromArmStatus(status);
            }
        }
    }
};
```

### 2. 在MCP工具中触发更新

```cpp
// 启动机械臂时更新状态
mcp_server.AddTool("self.arm.enable", "启动机械臂", PropertyList(), [this](const PropertyList&) -> ReturnValue {
    if (deep_arm_->enableArm()) {
        UpdateArmStatus();  // 更新灯带状态
        return std::string("启动机械臂成功");
    }
    return std::string("启动机械臂失败");
});
```

## 状态优先级

当多个状态同时存在时，系统按以下优先级处理：

1. **错误状态** - 最高优先级
2. **边界标定中** - 高优先级
3. **录制中** - 中高优先级
4. **播放中** - 中优先级
5. **移动中** - 中低优先级
6. **其他状态** - 低优先级

## 注意事项

1. **资源管理**：确保在析构时正确删除ArmLedState对象
2. **任务优先级**：状态更新任务优先级设置为3，避免影响关键任务
3. **更新频率**：默认500ms更新一次，可根据需要调整
4. **线程安全**：灯带操作已包含互斥锁保护
5. **错误处理**：状态更新失败时会记录日志但不影响机械臂操作

## 扩展功能

### 1. 添加新状态

```cpp
// 在ArmLedState::ArmState枚举中添加新状态
enum class ArmState {
    // ... 现有状态 ...
    CUSTOM_STATE  // 新状态
};

// 在default_mappings_数组中添加映射
{
    ArmState::CUSTOM_STATE,
    {LedEffect::BLINK, {255, 255, 0}, {0, 0, 0}, 500, 0, false},
    "自定义状态 - 黄色闪烁"
}
```

### 2. 添加新效果

```cpp
// 在LedEffect枚举中添加新效果
enum class LedEffect {
    // ... 现有效果 ...
    CUSTOM_EFFECT  // 新效果
};

// 在ApplyLedEffect函数中添加处理逻辑
case LedEffect::CUSTOM_EFFECT:
    // 实现自定义效果
    break;
```

## 调试信息

系统会输出详细的调试信息：

```
I (12345) ArmLedState: 机械臂状态变化: 0 -> 1
I (12346) ArmLedState: 应用灯带效果: 呼吸灯
I (12347) DeepArmControl: 启动机械臂成功
```

通过这些信息可以跟踪状态变化和灯带效果的切换过程。
