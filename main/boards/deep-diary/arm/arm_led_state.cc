#include "arm_led_state.h"
#include <esp_log.h>

#define TAG "ArmLedState"

// 默认状态-效果映射配置
const ArmLedState::StateLedMapping ArmLedState::default_mappings_[] = {
    // 未初始化 - 红色慢闪
    {
        ArmState::UNINITIALIZED,
        {LedEffect::BLINK, {255, 0, 0}, {0, 0, 0}, 1000, 0, false},
        "未初始化 - 红色慢闪"
    },
    
    // 已初始化但未启动 - 橙色呼吸灯
    {
        ArmState::INITIALIZED,
        {LedEffect::BREATHE, {255, 128, 0}, {128, 64, 0}, 2000, 0, false},
        "已初始化 - 橙色呼吸灯"
    },
    
    // 已启动运行中 - 绿色常亮
    {
        ArmState::ENABLED,
        {LedEffect::SOLID_COLOR, {0, 255, 0}, {0, 0, 0}, 0, 0, false},
        "已启动 - 绿色常亮"
    },
    
    // 录制中 - 蓝色快闪
    {
        ArmState::RECORDING,
        {LedEffect::BLINK, {0, 0, 255}, {0, 0, 0}, 200, 0, false},
        "录制中 - 蓝色快闪"
    },
    
    // 播放中 - 紫色跑马灯
    {
        ArmState::PLAYING,
        {LedEffect::SCROLL, {255, 0, 255}, {16, 0, 16}, 100, 3, false},
        "播放中 - 紫色跑马灯"
    },
    
    // 移动中 - 青色呼吸灯
    {
        ArmState::MOVING,
        {LedEffect::BREATHE, {0, 255, 255}, {0, 128, 128}, 500, 0, false},
        "移动中 - 青色呼吸灯"
    },
    
    // 错误状态 - 红色快闪
    {
        ArmState::ERROR,
        {LedEffect::BLINK, {255, 0, 0}, {0, 0, 0}, 100, 0, false},
        "错误状态 - 红色快闪"
    },
    
    // 边界标定中 - 黄色跑马灯
    {
        ArmState::BOUNDARY_CALIBRATING,
        {LedEffect::SCROLL, {255, 255, 0}, {16, 16, 0}, 300, 2, false},
        "边界标定中 - 黄色跑马灯"
    },
    
    // 边界未标定 - 橙色慢闪
    {
        ArmState::BOUNDARY_NOT_CALIBRATED,
        {LedEffect::BLINK, {255, 150, 0}, {0, 0, 0}, 2000, 0, false},
        "边界未标定 - 橙色慢闪"
    },
    
    // 边界已标定 - 绿色常亮
    {
        ArmState::BOUNDARY_CALIBRATED,
        {LedEffect::SOLID_COLOR, {0, 255, 0}, {0, 0, 0}, 0, 0, false},
        "边界已标定 - 绿色常亮"
    }
};

const int ArmLedState::mapping_count_ = sizeof(default_mappings_) / sizeof(default_mappings_[0]);

ArmLedState::ArmLedState(CircularStrip* led_strip) 
    : led_strip_(led_strip), current_state_(ArmState::UNINITIALIZED) {
    ESP_LOGI(TAG, "机械臂灯带状态管理器初始化");
}

ArmLedState::~ArmLedState() {
    StopCurrentEffect();
}

void ArmLedState::SetArmState(ArmState state) {
    if (state != current_state_) {
        OnStateChanged(state);
    }
}

void ArmLedState::UpdateFromArmStatus(const arm_status_t& status) {
    ArmState new_state = ArmState::UNINITIALIZED;
    
    // 根据机械臂状态结构体确定当前状态
    if (status.has_error) {
        new_state = ArmState::ERROR;
    } else if (status.boundary_status == BOUNDARY_CALIBRATING) {
        new_state = ArmState::BOUNDARY_CALIBRATING;
    } else if (status.boundary_status == BOUNDARY_NOT_CALIBRATED) {
        new_state = ArmState::BOUNDARY_NOT_CALIBRATED;
    } else if (status.boundary_status == BOUNDARY_CALIBRATED) {
        if (status.is_playing) {
            new_state = ArmState::PLAYING;
        } else if (status.is_recording) {
            new_state = ArmState::RECORDING;
        } else if (status.is_moving) {
            new_state = ArmState::MOVING;
        } else if (status.is_enabled) {
            new_state = ArmState::ENABLED;
        } else if (status.is_initialized) {
            new_state = ArmState::INITIALIZED;
        } else {
            new_state = ArmState::UNINITIALIZED;
        }
    } else {
        // 边界状态未知时的处理
        if (status.is_playing) {
            new_state = ArmState::PLAYING;
        } else if (status.is_recording) {
            new_state = ArmState::RECORDING;
        } else if (status.is_moving) {
            new_state = ArmState::MOVING;
        } else if (status.is_enabled) {
            new_state = ArmState::ENABLED;
        } else if (status.is_initialized) {
            new_state = ArmState::INITIALIZED;
        } else {
            new_state = ArmState::UNINITIALIZED;
        }
    }
    
    SetArmState(new_state);
}

void ArmLedState::StopCurrentEffect() {
    if (led_strip_) {
        led_strip_->SetAllColor({0, 0, 0});  // 关闭所有LED
    }
}

void ArmLedState::SetStateChangeCallback(std::function<void(ArmState, const LedEffectConfig&)> callback) {
    state_change_callback_ = callback;
}

void ArmLedState::ApplyLedEffect(const LedEffectConfig& config) {
    if (!led_strip_) {
        ESP_LOGW(TAG, "灯带指针为空，无法应用效果");
        return;
    }
    
    ESP_LOGI(TAG, "应用灯带效果: %s", 
             config.effect_type == LedEffect::OFF ? "关闭" :
             config.effect_type == LedEffect::SOLID_COLOR ? "纯色" :
             config.effect_type == LedEffect::BLINK ? "闪烁" :
             config.effect_type == LedEffect::BREATHE ? "呼吸灯" :
             config.effect_type == LedEffect::SCROLL ? "跑马灯" :
             config.effect_type == LedEffect::RAINBOW ? "彩虹" : "状态指示器");
    
    switch (config.effect_type) {
        case LedEffect::OFF:
            led_strip_->SetAllColor({0, 0, 0});
            break;
            
        case LedEffect::SOLID_COLOR:
            led_strip_->SetAllColor(config.primary_color);
            break;
            
        case LedEffect::BLINK:
            led_strip_->Blink(config.primary_color, config.interval_ms);
            break;
            
        case LedEffect::BREATHE:
            led_strip_->Breathe(config.primary_color, config.secondary_color, config.interval_ms);
            break;
            
        case LedEffect::SCROLL:
            led_strip_->Scroll(config.primary_color, config.secondary_color, 
                              config.length, config.interval_ms);
            break;
            
        case LedEffect::RAINBOW:
            // 彩虹效果暂时用跑马灯实现
            led_strip_->Scroll({255, 0, 0}, {0, 255, 0}, 2, config.interval_ms);
            break;
            
        case LedEffect::STATE_INDICATOR:
            // 状态指示器效果
            led_strip_->SetAllColor(config.primary_color);
            break;
    }
}

ArmLedState::LedEffectConfig ArmLedState::GetLedConfigForState(ArmState state) const {
    for (int i = 0; i < mapping_count_; i++) {
        if (default_mappings_[i].arm_state == state) {
            return default_mappings_[i].led_config;
        }
    }
    
    // 默认返回关闭状态
    return {LedEffect::OFF, {0, 0, 0}, {0, 0, 0}, 0, 0, true};
}

void ArmLedState::OnStateChanged(ArmState new_state) {
    ESP_LOGI(TAG, "机械臂状态变化: %d -> %d", static_cast<int>(current_state_), static_cast<int>(new_state));
    
    current_state_ = new_state;
    current_effect_ = GetLedConfigForState(new_state);
    
    // 应用新的灯带效果
    ApplyLedEffect(current_effect_);
    
    // 调用状态变化回调
    if (state_change_callback_) {
        state_change_callback_(new_state, current_effect_);
    }
}
