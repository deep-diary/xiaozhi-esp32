#pragma once

#include <cstdint>
#include <functional>

#include <driver/gpio.h>
#include <driver/touch_pad.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

enum class TouchButtonEvent : uint8_t {
    kPress = 0,
    kRelease,
    kLongPress,
};

using TouchButtonEventCallback =
    std::function<void(int button_id,
                       TouchButtonEvent event,
                       uint32_t value,
                       uint32_t baseline,
                       uint32_t abs_diff)>;

class TouchButtonController {
public:
    bool Initialize(gpio_num_t button1_gpio,
                    gpio_num_t button2_gpio,
                    gpio_num_t button3_gpio,
                    TouchButtonEventCallback callback);

private:
    static void TouchButtonsTask(void* arg);
    int FindChannelByGpio(int gpio) const;

    void DispatchEvent(int button_idx,
                       TouchButtonEvent event,
                       uint32_t value,
                       uint32_t baseline,
                       uint32_t abs_diff);

    touch_pad_t touch_pads_[3];
    uint32_t touch_baseline_[3] = {0};
    bool touch_pressed_[3] = {false};
    uint8_t touch_press_confirm_cnt_[3] = {0};
    uint8_t touch_release_confirm_cnt_[3] = {0};
    uint16_t touch_hold_cycles_[3] = {0};
    bool touch_longpress_fired_[3] = {false};
    TaskHandle_t touch_task_handle_ = nullptr;
    TouchButtonEventCallback event_callback_;
};

