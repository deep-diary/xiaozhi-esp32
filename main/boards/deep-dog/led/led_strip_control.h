#pragma once

#include "led/circular_strip.h"
#include "led/led_config.h"

#include <functional>

/**
 * WS2812 灯带控制层（模式 0–5、颜色、亮度）。
 * 不含 MCP / MQTT；协议适配（mqtt / mcp）与应用绑定经本类改灯效。
 */
class LedStripControl {
public:
    struct Status {
        int mode = DEEP_DOG_LED_MODE_OFF;
        uint8_t brightness = DEFAULT_BRIGHTNESS;
        uint8_t low_brightness = LOW_BRIGHTNESS;
        StripColor color{};
        StripColor low_color{};
        int interval_ms = 500;
        int scroll_length = 3;
        int led_count = 0;
        bool ok = false;
    };

    using StateChangedCb = std::function<void()>;

    explicit LedStripControl(CircularStrip* led_strip, int led_count);

    CircularStrip* strip() const { return led_strip_; }
    int led_count() const { return led_count_; }
    bool ok() const { return led_strip_ != nullptr && led_count_ > 0; }

    void SetStateChangedCallback(StateChangedCb cb) { on_changed_ = std::move(cb); }

    void SetBrightness(uint8_t brightness, uint8_t low_brightness);

    void ApplyOff();
    void ApplyStatic(StripColor color);
    void ApplyBlink(StripColor color, int interval_ms);
    void ApplyBreathe(StripColor low, StripColor high, int interval_ms);
    void ApplyScroll(StripColor low, StripColor high, int length, int interval_ms);
    /** mode=5：交还应用绑定；可选触发设备状态灯效 */
    void ApplySystem(bool trigger_device_state = false);

    /** 仅同步状态快照（外部已直接操 strip 时用） */
    void UpdateState(int mode, StripColor color, int interval_ms = 500, int scroll_length = 3);
    void UpdateState(int mode, StripColor main_color, StripColor low_color, int interval_ms = 500,
                     int scroll_length = 3);

    Status GetStatus() const;

    int GetCurrentMode() const { return current_mode_; }
    bool IsAppBound() const { return current_mode_ == DEEP_DOG_LED_MODE_SYSTEM; }
    bool IsManualOverride() const {
        return current_mode_ >= DEEP_DOG_LED_MODE_OFF && current_mode_ <= DEEP_DOG_LED_MODE_SCROLL;
    }

private:
    void NotifyChanged();

    CircularStrip* led_strip_ = nullptr;
    int led_count_ = 0;
    StateChangedCb on_changed_;

    int current_mode_ = DEEP_DOG_LED_MODE_OFF;
    StripColor current_color_{};
    StripColor low_color_{};
    int current_interval_ms_ = 500;
    int current_scroll_length_ = 3;
    uint8_t default_brightness_ = DEFAULT_BRIGHTNESS;
    uint8_t low_brightness_ = LOW_BRIGHTNESS;
};
