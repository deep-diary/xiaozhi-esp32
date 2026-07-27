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

/** A 类检测灵敏度（运行时可调 + NVS） */
struct TouchThresholds {
    float press_abs_ratio = DEEP_DOG_TOUCH_PRESS_ABS_RATIO;
    float release_abs_ratio = DEEP_DOG_TOUCH_RELEASE_ABS_RATIO;
    uint32_t press_abs_offset = DEEP_DOG_TOUCH_PRESS_ABS_OFFSET;
    uint32_t release_abs_offset = DEEP_DOG_TOUCH_RELEASE_ABS_OFFSET;
    uint32_t press_abs_min[3] = {DEEP_DOG_TOUCH_PRESS_ABS_MIN_1, DEEP_DOG_TOUCH_PRESS_ABS_MIN_2,
                                 DEEP_DOG_TOUCH_PRESS_ABS_MIN_3};
    uint32_t release_abs_min[3] = {DEEP_DOG_TOUCH_RELEASE_ABS_MIN_1, DEEP_DOG_TOUCH_RELEASE_ABS_MIN_2,
                                   DEEP_DOG_TOUCH_RELEASE_ABS_MIN_3};
    float baseline_alpha = DEEP_DOG_TOUCH_BASELINE_ALPHA;
    float baseline_update_abs_ratio = DEEP_DOG_TOUCH_BASELINE_UPDATE_ABS_RATIO;
    uint32_t baseline_update_abs_offset = DEEP_DOG_TOUCH_BASELINE_UPDATE_ABS_OFFSET;
    uint8_t debounce_cycles = DEEP_DOG_TOUCH_DEBOUNCE_CYCLES;
};

enum class TouchCalibPhase : uint8_t {
    kIdle = 0,
    kCollect,
    kDone,
    kFail,
};

struct TouchCalibStatus {
    bool active = false;
    int button_id = 0;  // 1..3
    TouchCalibPhase phase = TouchCalibPhase::kIdle;
    int count = 0;
    int samples = 0;
    const char* error = nullptr;  // static string when fail
};

using TouchButtonEventCallback =
    std::function<void(int button_id,
                       TouchButtonEvent event,
                       uint32_t value,
                       uint32_t baseline,
                       uint32_t abs_diff)>;

/**
 * 三键电容触摸驱动：采样 + press/release/long/short/double。
 * 阈值可运行时调整 / NVS / 单键软检测标定。
 */
class TouchButtonController {
public:
    static TouchThresholds FactoryThresholds();

    bool Initialize(gpio_num_t button1_gpio,
                    gpio_num_t button2_gpio,
                    gpio_num_t button3_gpio,
                    TouchButtonEventCallback callback);

    /** 开机加载 NVS（失败则保持出厂默认） */
    bool LoadThresholdsFromNvs();

    uint8_t GetPressedMask() const;
    bool GetButtonState(int button_id, TouchButtonState* out) const;

    TouchThresholds GetThresholds() const;
    bool SetThresholds(const TouchThresholds& thr, bool persist);
    bool ResetThresholdsToFactory(bool persist);

    /** 单键标定；samples 默认 10，夹到 [1, MAX] */
    bool StartCalibrate(int button_id, int samples = DEEP_DOG_TOUCH_CALIB_DEFAULT_SAMPLES);
    void CancelCalibrate();
    TouchCalibStatus GetCalibStatus() const;

    /** MQTT 强制 debug[]；标定中也视为 true */
    void SetMqttDebug(bool on) { mqtt_debug_ = on; }
    bool WantMqttDebug() const;

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

    TouchThresholds CopyThresholds() const;
    void ApplyThresholdsLocked(const TouchThresholds& thr);
    bool PersistThresholdsUnlocked(const TouchThresholds& thr);
    void TickCalibrate(int button_idx, uint32_t abs_diff);
    void FailCalibrate(const char* err);

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
    TouchThresholds thresholds_ = FactoryThresholds();
    mutable portMUX_TYPE snapshot_mux_ = portMUX_INITIALIZER_UNLOCKED;

    // calib (touched only from touch task + Start/Cancel under mux)
    bool calib_active_ = false;
    int calib_button_idx_ = -1;
    TouchCalibPhase calib_phase_ = TouchCalibPhase::kIdle;
    int calib_count_ = 0;
    int calib_samples_ = 0;
    int64_t calib_deadline_us_ = 0;
    int64_t calib_idle_until_us_ = 0;
    uint32_t calib_idle_peak_ = 0;
    uint32_t calib_soft_thr_ = 80;
    uint32_t calib_accept_min_ = 400;  // peak must reach this to count
    bool calib_in_peak_ = false;
    uint32_t calib_peak_cur_ = 0;
    int64_t calib_refractory_until_us_ = 0;
    uint32_t calib_peaks_[DEEP_DOG_TOUCH_CALIB_MAX_SAMPLES] = {};
    const char* calib_error_ = nullptr;

    bool mqtt_debug_ = false;

    TaskHandle_t touch_task_handle_ = nullptr;
    TouchButtonEventCallback event_callback_;
};
