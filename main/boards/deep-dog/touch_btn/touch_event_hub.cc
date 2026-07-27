#include "touch_btn/touch_event_hub.h"

#include <esp_log.h>
#include <esp_timer.h>

#define TAG "touch_hub"

TouchEventHub::TouchEventHub() = default;

TouchEventHub::~TouchEventHub() {
    if (queue_) {
        vQueueDelete(queue_);
        queue_ = nullptr;
    }
}

bool TouchEventHub::Init(UBaseType_t depth) {
    if (queue_) {
        return true;
    }
    queue_ = xQueueCreate(depth, sizeof(TouchEvent));
    if (!queue_) {
        ESP_LOGE(TAG, "TouchEventHub queue create failed");
        return false;
    }
    for (int i = 0; i < 3; i++) {
        snapshot_[i] = TouchButtonState{};
    }
    pressed_mask_ = 0;
    return true;
}

void TouchEventHub::SetPushListener(PushListener listener) {
    listener_ = std::move(listener);
}

bool TouchEventHub::Push(const TouchEvent& ev) {
    if (!queue_) {
        return false;
    }

    TouchEvent stored = ev;
    if (stored.ts_us == 0) {
        stored.ts_us = esp_timer_get_time();
    }

    portENTER_CRITICAL(&mux_);
    if (stored.button_id >= 1 && stored.button_id <= 3) {
        TouchButtonState& s = snapshot_[stored.button_id - 1];
        s.last_event = stored.event;
        s.value = stored.value;
        s.baseline = stored.baseline;
        s.abs_diff = stored.abs_diff;
        switch (stored.event) {
            case TouchButtonEvent::kPress:
                s.pressed = true;
                s.long_press = false;
                break;
            case TouchButtonEvent::kRelease:
                s.pressed = false;
                s.long_press = false;
                break;
            case TouchButtonEvent::kLongPress:
                s.pressed = true;
                s.long_press = true;
                break;
            case TouchButtonEvent::kShortPress:
            case TouchButtonEvent::kDoubleClick:
                s.pressed = false;
                s.long_press = false;
                break;
        }
        pressed_mask_ = 0;
        for (int i = 0; i < 3; i++) {
            if (snapshot_[i].pressed) {
                pressed_mask_ |= static_cast<uint8_t>(1u << i);
            }
        }
        stored.pressed_mask = pressed_mask_;
    } else {
        stored.pressed_mask = pressed_mask_;
    }
    portEXIT_CRITICAL(&mux_);

    if (xQueueSend(queue_, &stored, 0) != pdTRUE) {
        ESP_LOGW(TAG, "TouchEventHub queue full, drop event btn=%d", stored.button_id);
        // 仍通知 listener，保证 MQTT 快照不丢最新态
    }

    if (listener_) {
        listener_(stored);
    }
    return true;
}

bool TouchEventHub::Pop(TouchEvent* out, TickType_t wait) {
    if (!queue_ || !out) {
        return false;
    }
    return xQueueReceive(queue_, out, wait) == pdTRUE;
}

void TouchEventHub::UpdateFromController(const TouchButtonController& ctrl) {
    TouchButtonState local[3];
    const uint8_t mask = ctrl.GetPressedMask();
    for (int i = 0; i < 3; i++) {
        ctrl.GetButtonState(i + 1, &local[i]);
    }
    portENTER_CRITICAL(&mux_);
    pressed_mask_ = mask;
    for (int i = 0; i < 3; i++) {
        snapshot_[i] = local[i];
    }
    portEXIT_CRITICAL(&mux_);
}

uint8_t TouchEventHub::GetPressedMask() const {
    portENTER_CRITICAL(&mux_);
    const uint8_t m = pressed_mask_;
    portEXIT_CRITICAL(&mux_);
    return m;
}

bool TouchEventHub::GetButtonState(int button_id, TouchButtonState* out) const {
    if (!out || button_id < 1 || button_id > 3) {
        return false;
    }
    portENTER_CRITICAL(&mux_);
    *out = snapshot_[button_id - 1];
    portEXIT_CRITICAL(&mux_);
    return true;
}
