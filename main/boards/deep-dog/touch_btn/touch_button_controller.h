#pragma once

#include "touch_btn/touch_config.h"

#include <cstdint>
#include <functional>

#include <driver/gpio.h>
#include <driver/touch_pad.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <freertos/task.h>

enum class TouchButtonEvent : uint8_t {
    kPress = 0,
    kRelease,
    kLongPress,
    kShortPress,
    kDoubleClick,
};

struct TouchButtonState {
    bool pressed = false;
    bool long_press = false;
    TouchButtonEvent last_event = TouchButtonEvent::kRelease;
    uint32_t value = 0;
    uint32_t baseline = 0;
    uint32_t abs_diff = 0;
};

using TouchButtonEventCallback =
    std::function<void(int button_id,
                       TouchButtonEvent event,
                       uint32_t value,
                       uint32_t baseline,
                       uint32_t abs_diff)>;

/**
 * 三键电容触摸驱动：采样 + press/release/long/short/double。
 * 仅通过 callback 上报；业务与 MQTT 由上层 Hub / Dispatcher 消费。
 */
class TouchButtonController {
public:
    bool Initialize(gpio_num_t button1_gpio,
                    gpio_num_t button2_gpio,
                    gpio_num_t button3_gpio,
                    TouchButtonEventCallback callback);

    /** bit0/1/2 = 键 1/2/3 当前按下 */
    uint8_t GetPressedMask() const;

    /** button_id ∈ {1,2,3} */
    bool GetButtonState(int button_id, TouchButtonState* out) const;

private:
    static void TouchButtonsTask(void* arg);
    int FindChannelByGpio(int gpio) const;

    void DispatchEvent(int button_idx,
                       TouchButtonEvent event,
                       uint32_t value,
                       uint32_t baseline,
                       uint32_t abs_diff);
    void UpdateSnapshotLocked(int button_idx,
                              TouchButtonEvent event,
                              uint32_t value,
                              uint32_t baseline,
                              uint32_t abs_diff);
    void FlushPendingShortPress(int button_idx,
                                uint32_t value,
                                uint32_t baseline,
                                uint32_t abs_diff);
    void PollDoubleClickWindows();

    touch_pad_t touch_pads_[3];
    uint32_t touch_baseline_[3] = {0};
    bool touch_pressed_[3] = {false};
    uint8_t touch_press_confirm_cnt_[3] = {0};
    uint8_t touch_release_confirm_cnt_[3] = {0};
    uint16_t touch_hold_cycles_[3] = {0};
    bool touch_longpress_fired_[3] = {false};

    bool pending_short_[3] = {false};
    int64_t pending_short_deadline_us_[3] = {0};
    uint32_t pending_short_value_[3] = {0};
    uint32_t pending_short_baseline_[3] = {0};
    uint32_t pending_short_abs_diff_[3] = {0};
    bool click_armed_[3] = {false};

    TouchButtonState snapshot_[3];
    mutable portMUX_TYPE snapshot_mux_ = portMUX_INITIALIZER_UNLOCKED;

    TaskHandle_t touch_task_handle_ = nullptr;
    TouchButtonEventCallback event_callback_;
};
