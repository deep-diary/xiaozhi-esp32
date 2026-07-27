#pragma once

#include "touch_btn/touch_button_controller.h"
#include "touch_btn/touch_config.h"

#include <cstdint>
#include <functional>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/portmacro.h>

struct TouchEvent {
    int button_id = 0;
    TouchButtonEvent event = TouchButtonEvent::kRelease;
    uint32_t value = 0;
    uint32_t baseline = 0;
    uint32_t abs_diff = 0;
    uint8_t pressed_mask = 0;
    int64_t ts_us = 0;
};

/**
 * 驱动事件入队 + 三键快照；MQTT / Dispatcher 从此消费。
 */
class TouchEventHub {
public:
    using PushListener = std::function<void(const TouchEvent&)>;

    TouchEventHub();
    ~TouchEventHub();

    TouchEventHub(const TouchEventHub&) = delete;
    TouchEventHub& operator=(const TouchEventHub&) = delete;

    bool Init(UBaseType_t depth = DEEP_DOG_TOUCH_EVENT_QUEUE_DEPTH);

    /** 驱动回调线程安全 Push；可选同步通知 MQTT */
    bool Push(const TouchEvent& ev);

    bool Pop(TouchEvent* out, TickType_t wait = 0);

    void SetPushListener(PushListener listener);

    void UpdateFromController(const TouchButtonController& ctrl);
    uint8_t GetPressedMask() const;
    bool GetButtonState(int button_id, TouchButtonState* out) const;

private:
    QueueHandle_t queue_ = nullptr;
    PushListener listener_;
    TouchButtonState snapshot_[3];
    uint8_t pressed_mask_ = 0;
    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
};
