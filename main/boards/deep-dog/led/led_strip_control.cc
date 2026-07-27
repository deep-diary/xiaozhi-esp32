#include "led/led_strip_control.h"

LedStripControl::LedStripControl(CircularStrip* led_strip, int led_count)
    : led_strip_(led_strip), led_count_(led_count) {
    if (led_strip_) {
        led_strip_->SetBrightness(default_brightness_, low_brightness_);
    }
}

void LedStripControl::NotifyChanged() {
    if (on_changed_) {
        on_changed_();
    }
}

void LedStripControl::SetBrightness(uint8_t brightness, uint8_t low_brightness) {
    default_brightness_ = brightness;
    low_brightness_ = low_brightness;
    if (led_strip_) {
        led_strip_->SetBrightness(default_brightness_, low_brightness_);
    }
    NotifyChanged();
}

void LedStripControl::ApplyOff() {
    current_mode_ = DEEP_DOG_LED_MODE_OFF;
    current_color_ = {0, 0, 0};
    if (led_strip_) {
        led_strip_->SetAllColor(current_color_);
    }
    NotifyChanged();
}

void LedStripControl::ApplyStatic(StripColor color) {
    /* mode=1 保持静态，即使 RGB 全 0（黑灯）；关灯只能走 ApplyOff / mode=0 */
    current_mode_ = DEEP_DOG_LED_MODE_STATIC;
    current_color_ = color;
    if (led_strip_) {
        led_strip_->SetAllColor(color);
    }
    NotifyChanged();
}

void LedStripControl::ApplyBlink(StripColor color, int interval_ms) {
    if (interval_ms < 0) {
        interval_ms = 0;
    }
    current_mode_ = DEEP_DOG_LED_MODE_BLINK;
    current_color_ = color;
    current_interval_ms_ = interval_ms;
    if (led_strip_) {
        led_strip_->Blink(color, interval_ms);
    }
    NotifyChanged();
}

void LedStripControl::ApplyBreathe(StripColor low, StripColor high, int interval_ms) {
    if (interval_ms < 10) {
        interval_ms = 10;
    }
    current_mode_ = DEEP_DOG_LED_MODE_BREATHE;
    current_color_ = high;
    low_color_ = low;
    current_interval_ms_ = interval_ms;
    if (led_strip_) {
        led_strip_->Breathe(low, high, interval_ms);
    }
    NotifyChanged();
}

void LedStripControl::ApplyScroll(StripColor low, StripColor high, int length, int interval_ms) {
    if (length < 1) {
        length = 1;
    }
    if (led_count_ > 0 && length > led_count_) {
        length = led_count_;
    }
    if (interval_ms < 0) {
        interval_ms = 0;
    }
    current_mode_ = DEEP_DOG_LED_MODE_SCROLL;
    current_color_ = high;
    low_color_ = low;
    current_scroll_length_ = length;
    current_interval_ms_ = interval_ms;
    if (led_strip_) {
        led_strip_->Scroll(low, high, length, interval_ms);
    }
    NotifyChanged();
}

void LedStripControl::ApplySystem(bool trigger_device_state) {
    current_mode_ = DEEP_DOG_LED_MODE_SYSTEM;
    if (trigger_device_state && led_strip_) {
        led_strip_->OnStateChanged();
    }
    NotifyChanged();
}

void LedStripControl::UpdateState(int mode, StripColor color, int interval_ms, int scroll_length) {
    current_mode_ = mode;
    current_color_ = color;
    current_interval_ms_ = interval_ms;
    current_scroll_length_ = scroll_length;
    NotifyChanged();
}

void LedStripControl::UpdateState(int mode, StripColor main_color, StripColor low_color,
                                  int interval_ms, int scroll_length) {
    current_mode_ = mode;
    current_color_ = main_color;
    low_color_ = low_color;
    current_interval_ms_ = interval_ms;
    current_scroll_length_ = scroll_length;
    NotifyChanged();
}

LedStripControl::Status LedStripControl::GetStatus() const {
    Status s;
    s.mode = current_mode_;
    s.brightness = default_brightness_;
    s.low_brightness = low_brightness_;
    s.color = current_color_;
    s.low_color = low_color_;
    s.interval_ms = current_interval_ms_;
    s.scroll_length = current_scroll_length_;
    s.led_count = led_count_;
    s.ok = ok();
    return s;
}
