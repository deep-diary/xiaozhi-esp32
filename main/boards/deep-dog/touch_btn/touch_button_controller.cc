#include "touch_btn/touch_button_controller.h"

#include <esp_log.h>
#include <soc/touch_sensor_periph.h>

#include <algorithm>
#include <freertos/FreeRTOS.h>

#define TAG "deep_dog_touch"

int TouchButtonController::FindChannelByGpio(int gpio) const {
    for (int ch = 0; ch < (int)TOUCH_PAD_MAX; ch++) {
        if (touch_sensor_channel_io_map[ch] == gpio) {
            return ch;
        }
    }
    return -1;
}

void TouchButtonController::DispatchEvent(int button_idx,
                                          TouchButtonEvent event,
                                          uint32_t value,
                                          uint32_t baseline,
                                          uint32_t abs_diff) {
    if (!event_callback_) {
        return;
    }
    event_callback_(button_idx + 1, event, value, baseline, abs_diff);
}

bool TouchButtonController::Initialize(gpio_num_t button1_gpio,
                                       gpio_num_t button2_gpio,
                                       gpio_num_t button3_gpio,
                                       TouchButtonEventCallback callback) {
    event_callback_ = callback;

    touch_pad_init();

    int ch1 = FindChannelByGpio((int)button1_gpio);
    int ch2 = FindChannelByGpio((int)button2_gpio);
    int ch3 = FindChannelByGpio((int)button3_gpio);
    if (ch1 < 0 || ch2 < 0 || ch3 < 0) {
        ESP_LOGE(TAG, "Touch pad channel map failed: gpio=%d/%d/%d => ch=%d/%d/%d",
                 (int)button1_gpio, (int)button2_gpio, (int)button3_gpio, ch1, ch2, ch3);
        return false;
    }
    touch_pads_[0] = static_cast<touch_pad_t>(ch1);
    touch_pads_[1] = static_cast<touch_pad_t>(ch2);
    touch_pads_[2] = static_cast<touch_pad_t>(ch3);

    touch_pad_set_voltage(TOUCH_HVOLT_2V7, TOUCH_LVOLT_0V5, TOUCH_HVOLT_ATTEN_1V);

    for (int i = 0; i < 3; i++) {
        touch_pad_config(touch_pads_[i]);
        touch_pressed_[i] = false;
        touch_press_confirm_cnt_[i] = 0;
        touch_release_confirm_cnt_[i] = 0;
        touch_hold_cycles_[i] = 0;
        touch_longpress_fired_[i] = false;
    }

    touch_pad_filter_enable();
    touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER);
    touch_pad_fsm_start();
    vTaskDelay(pdMS_TO_TICKS(200));

    for (int i = 0; i < 3; i++) {
        uint64_t sum = 0;
        const int samples = 30;
        for (int k = 0; k < samples; k++) {
            uint32_t value = 0;
            touch_pad_read_raw_data(touch_pads_[i], &value);
            sum += (uint64_t)value;
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        touch_baseline_[i] = (uint32_t)(sum / samples);
        ESP_LOGI(TAG, "Touch button %d baseline=%u", i + 1, (unsigned)touch_baseline_[i]);
    }

    xTaskCreate(TouchButtonsTask, "touch_btn_task", 4096, this, 4, &touch_task_handle_);
    return true;
}

void TouchButtonController::TouchButtonsTask(void* arg) {
    TouchButtonController* self = static_cast<TouchButtonController*>(arg);
    if (!self) {
        vTaskDelete(NULL);
        return;
    }

    // 这组阈值按当前板子的实测噪声调高，避免上电后未触摸就误判为 pressed。
    const float press_abs_ratio = 0.05f;
    const float release_abs_ratio = 0.03f;
    const uint32_t press_abs_min[3] = {2600, 1200, 900};
    const uint32_t release_abs_min[3] = {2200, 950, 700};
    const uint8_t confirm_cycles = 2;
    const float baseline_alpha = 0.01f;
    const float baseline_update_abs_ratio = 0.12f;
    const uint32_t baseline_update_abs_offset = 200;
    const uint16_t long_press_cycles = 12;

    // 仅打印按键状态变化（pressed/released/long-pressed）
    // 如需再次打开“raw/baseline/abs_diff”调试，把 enable_raw_debug 改为 true。
    const bool enable_raw_debug = false;
    TickType_t start_tick = xTaskGetTickCount();
    TickType_t last_debug_tick = start_tick;
    const TickType_t debug_window_ticks = pdMS_TO_TICKS(180000);
    const TickType_t debug_interval_ticks = pdMS_TO_TICKS(1000);
    const int debug_button_index = 1; // 仅打印 2 号按键 raw 调试值（enable_raw_debug=true 才生效）

    while (true) {
        const TickType_t now_tick = xTaskGetTickCount();
        const bool do_debug = enable_raw_debug &&
                               (now_tick - start_tick < debug_window_ticks) &&
                               (now_tick - last_debug_tick >= debug_interval_ticks);
        if (do_debug) {
            last_debug_tick = now_tick;
        }

        for (int i = 0; i < 3; i++) {
            uint32_t value = 0;
            (void)touch_pad_read_raw_data(self->touch_pads_[i], &value);
            const uint32_t baseline = self->touch_baseline_[i];
            if (baseline == 0) {
                continue;
            }

            const int64_t diff = (int64_t)value - (int64_t)baseline;
            const uint32_t abs_diff = (uint32_t)(diff >= 0 ? diff : -diff);

            if (do_debug && i == debug_button_index) {
                ESP_LOGI(TAG, "Touch button %d value=%u baseline=%u abs_diff=%u",
                         i + 1, (unsigned)value, (unsigned)baseline, (unsigned)abs_diff);
            }

            const uint32_t press_abs = std::max(
                (uint32_t)(baseline * press_abs_ratio) + 200,
                press_abs_min[i]);
            const uint32_t release_abs = std::max(
                (uint32_t)(baseline * release_abs_ratio) + 120,
                release_abs_min[i]);

            if (!self->touch_pressed_[i]) {
                if (abs_diff > press_abs) {
                    if (self->touch_press_confirm_cnt_[i] < 255) {
                        self->touch_press_confirm_cnt_[i]++;
                    }
                    if (self->touch_press_confirm_cnt_[i] >= confirm_cycles) {
                        self->touch_pressed_[i] = true;
                        self->touch_press_confirm_cnt_[i] = 0;
                        self->touch_release_confirm_cnt_[i] = 0;
                        self->touch_hold_cycles_[i] = 0;
                        self->touch_longpress_fired_[i] = false;
                        self->DispatchEvent(i, TouchButtonEvent::kPress, value, baseline, abs_diff);
                    }
                } else {
                    self->touch_press_confirm_cnt_[i] = 0;
                }

                const uint32_t baseline_update_abs =
                    (uint32_t)((float)baseline * baseline_update_abs_ratio) + baseline_update_abs_offset;
                if (abs_diff < baseline_update_abs) {
                    self->touch_baseline_[i] = (uint32_t)(
                        (1.0f - baseline_alpha) * (float)baseline + baseline_alpha * (float)value);
                }
            } else {
                if (abs_diff < release_abs) {
                    if (self->touch_release_confirm_cnt_[i] < 255) {
                        self->touch_release_confirm_cnt_[i]++;
                    }
                    if (self->touch_release_confirm_cnt_[i] >= confirm_cycles) {
                        self->touch_pressed_[i] = false;
                        self->touch_release_confirm_cnt_[i] = 0;
                        self->touch_hold_cycles_[i] = 0;
                        self->touch_longpress_fired_[i] = false;
                        self->DispatchEvent(i, TouchButtonEvent::kRelease, value, baseline, abs_diff);
                    }
                } else {
                    self->touch_release_confirm_cnt_[i] = 0;
                    if (self->touch_hold_cycles_[i] < 0xFFFF) {
                        self->touch_hold_cycles_[i]++;
                    }
                    if (!self->touch_longpress_fired_[i] &&
                        self->touch_hold_cycles_[i] >= long_press_cycles) {
                        self->touch_longpress_fired_[i] = true;
                        self->DispatchEvent(i, TouchButtonEvent::kLongPress, value, baseline, abs_diff);
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

