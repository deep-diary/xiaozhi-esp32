#ifndef _ARM_LED_STATE_H_
#define _ARM_LED_STATE_H_

#include "led/circular_strip.h"
#include "deep_arm.h"
#include <functional>

/**
 * @brief 机械臂灯带状态管理类
 * 
 * 功能：
 * 1. 定义机械臂各种状态对应的灯带效果
 * 2. 提供状态变化时的灯带效果自动切换
 * 3. 支持自定义状态-效果映射
 */
class ArmLedState {
public:
    // 机械臂状态枚举
    enum class ArmState {
        UNINITIALIZED,          // 未初始化
        INITIALIZED,            // 已初始化但未启动
        ENABLED,                // 已启动运行中
        RECORDING,              // 录制中
        PLAYING,                // 播放中
        MOVING,                 // 移动中
        ERROR,                  // 错误状态
        BOUNDARY_CALIBRATING,   // 边界标定中
        BOUNDARY_NOT_CALIBRATED, // 边界未标定
        BOUNDARY_CALIBRATED     // 边界已标定
    };

    // 灯带效果类型
    enum class LedEffect {
        OFF,                    // 关闭
        SOLID_COLOR,            // 纯色
        BLINK,                  // 闪烁
        BREATHE,                // 呼吸灯
        SCROLL,                 // 跑马灯
        RAINBOW,                // 彩虹效果
        STATE_INDICATOR         // 状态指示器
    };

    // 灯带效果配置
    struct LedEffectConfig {
        LedEffect effect_type;
        StripColor primary_color;
        StripColor secondary_color;
        int interval_ms;
        int length;  // 用于跑马灯效果
        bool auto_stop;  // 是否自动停止
    };

    // 状态-效果映射配置
    struct StateLedMapping {
        ArmState arm_state;
        LedEffectConfig led_config;
        std::string description;
    };

    ArmLedState(CircularStrip* led_strip);
    ~ArmLedState();

    // 设置机械臂状态并更新灯带效果
    void SetArmState(ArmState state);
    
    // 从机械臂状态结构体更新状态
    void UpdateFromArmStatus(const arm_status_t& status);
    
    // 获取当前状态
    ArmState GetCurrentState() const { return current_state_; }
    
    // 停止当前灯带效果
    void StopCurrentEffect();
    
    // 设置状态变化回调
    void SetStateChangeCallback(std::function<void(ArmState, const LedEffectConfig&)> callback);

private:
    CircularStrip* led_strip_;
    ArmState current_state_;
    LedEffectConfig current_effect_;
    std::function<void(ArmState, const LedEffectConfig&)> state_change_callback_;
    
    // 默认状态-效果映射配置
    static const StateLedMapping default_mappings_[];
    static const int mapping_count_;
    
    // 应用灯带效果
    void ApplyLedEffect(const LedEffectConfig& config);
    
    // 根据状态获取对应的灯带配置
    LedEffectConfig GetLedConfigForState(ArmState state) const;
    
    // 状态变化处理
    void OnStateChanged(ArmState new_state);
};

#endif // _ARM_LED_STATE_H_
