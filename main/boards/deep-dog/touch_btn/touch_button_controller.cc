#include "touch_btn/touch_button_controller.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <soc/touch_sensor_periph.h>

#include <algorithm>
#include <freertos/FreeRTOS.h>

#define TAG "deep_dog_touch"

namespace {

struct TouchThrNvsBlob {
    uint8_t ver = DEEP_DOG_TOUCH_THR_NVS_VER;
    uint8_t debounce_cycles = 0;
    uint8_t _pad[2] = {};
    float press_abs_ratio = 0;
    float release_abs_ratio = 0;
    uint32_t press_abs_offset = 0;
    uint32_t release_abs_offset = 0;
    uint32_t press_abs_min[3] = {};
    uint32_t release_abs_min[3] = {};
    float baseline_alpha = 0;
    float baseline_update_abs_ratio = 0;
    uint32_t baseline_update_abs_offset = 0;
};

TouchThrNvsBlob ToBlob(const TouchThresholds& t) {
    TouchThrNvsBlob b;
    b.ver = DEEP_DOG_TOUCH_THR_NVS_VER;
    b.debounce_cycles = t.debounce_cycles;
    b.press_abs_ratio = t.press_abs_ratio;
    b.release_abs_ratio = t.release_abs_ratio;
    b.press_abs_offset = t.press_abs_offset;
    b.release_abs_offset = t.release_abs_offset;
    for (int i = 0; i < 3; i++) {
        b.press_abs_min[i] = t.press_abs_min[i];
        b.release_abs_min[i] = t.release_abs_min[i];
    }
    b.baseline_alpha = t.baseline_alpha;
    b.baseline_update_abs_ratio = t.baseline_update_abs_ratio;
    b.baseline_update_abs_offset = t.baseline_update_abs_offset;
    return b;
}

bool FromBlob(const TouchThrNvsBlob& b, TouchThresholds* out) {
    if (!out || b.ver != DEEP_DOG_TOUCH_THR_NVS_VER) {
        return false;
    }
    *out = TouchButtonController::FactoryThresholds();
    out->debounce_cycles = b.debounce_cycles ? b.debounce_cycles : DEEP_DOG_TOUCH_DEBOUNCE_CYCLES;
    out->press_abs_ratio = b.press_abs_ratio;
    out->release_abs_ratio = b.release_abs_ratio;
    out->press_abs_offset = b.press_abs_offset;
    out->release_abs_offset = b.release_abs_offset;
    for (int i = 0; i < 3; i++) {
        out->press_abs_min[i] = b.press_abs_min[i];
        out->release_abs_min[i] = b.release_abs_min[i];
    }
    out->baseline_alpha = b.baseline_alpha;
    out->baseline_update_abs_ratio = b.baseline_update_abs_ratio;
    out->baseline_update_abs_offset = b.baseline_update_abs_offset;
    return true;
}

void SanitizeThresholds(TouchThresholds* t) {
    if (!t) {
        return;
    }
    if (t->press_abs_ratio < 0.001f) {
        t->press_abs_ratio = 0.001f;
    }
    if (t->press_abs_ratio > 0.5f) {
        t->press_abs_ratio = 0.5f;
    }
    if (t->release_abs_ratio < 0.001f) {
        t->release_abs_ratio = 0.001f;
    }
    if (t->release_abs_ratio > t->press_abs_ratio) {
        t->release_abs_ratio = t->press_abs_ratio;
    }
    if (t->debounce_cycles < 1) {
        t->debounce_cycles = 1;
    }
    if (t->debounce_cycles > 10) {
        t->debounce_cycles = 10;
    }
    if (t->baseline_alpha < 0.0f) {
        t->baseline_alpha = 0.0f;
    }
    if (t->baseline_alpha > 0.5f) {
        t->baseline_alpha = 0.5f;
    }
    for (int i = 0; i < 3; i++) {
        if (t->press_abs_min[i] < 150) {
            t->press_abs_min[i] = 150;
        }
        if (t->release_abs_min[i] < 80) {
            t->release_abs_min[i] = 80;
        }
        if (t->release_abs_min[i] > t->press_abs_min[i]) {
            t->release_abs_min[i] = (t->press_abs_min[i] * 3) / 4;
        }
    }
}

}  // namespace

TouchThresholds TouchButtonController::FactoryThresholds() {
    TouchThresholds t;
    t.press_abs_ratio = DEEP_DOG_TOUCH_PRESS_ABS_RATIO;
    t.release_abs_ratio = DEEP_DOG_TOUCH_RELEASE_ABS_RATIO;
    t.press_abs_offset = DEEP_DOG_TOUCH_PRESS_ABS_OFFSET;
    t.release_abs_offset = DEEP_DOG_TOUCH_RELEASE_ABS_OFFSET;
    t.press_abs_min[0] = DEEP_DOG_TOUCH_PRESS_ABS_MIN_1;
    t.press_abs_min[1] = DEEP_DOG_TOUCH_PRESS_ABS_MIN_2;
    t.press_abs_min[2] = DEEP_DOG_TOUCH_PRESS_ABS_MIN_3;
    t.release_abs_min[0] = DEEP_DOG_TOUCH_RELEASE_ABS_MIN_1;
    t.release_abs_min[1] = DEEP_DOG_TOUCH_RELEASE_ABS_MIN_2;
    t.release_abs_min[2] = DEEP_DOG_TOUCH_RELEASE_ABS_MIN_3;
    t.baseline_alpha = DEEP_DOG_TOUCH_BASELINE_ALPHA;
    t.baseline_update_abs_ratio = DEEP_DOG_TOUCH_BASELINE_UPDATE_ABS_RATIO;
    t.baseline_update_abs_offset = DEEP_DOG_TOUCH_BASELINE_UPDATE_ABS_OFFSET;
    t.debounce_cycles = DEEP_DOG_TOUCH_DEBOUNCE_CYCLES;
    return t;
}

int TouchButtonController::FindChannelByGpio(int gpio) const {
    for (int ch = 0; ch < (int)TOUCH_PAD_MAX; ch++) {
        if (touch_sensor_channel_io_map[ch] == gpio) {
            return ch;
        }
    }
    return -1;
}

void TouchButtonController::UpdateSnapshotLocked(int button_idx,
                                                 TouchButtonEvent event,
                                                 uint32_t value,
                                                 uint32_t baseline,
                                                 uint32_t abs_diff) {
    TouchButtonState& s = snapshot_[button_idx];
    s.last_event = event;
    s.value = value;
    s.baseline = baseline;
    s.abs_diff = abs_diff;
    switch (event) {
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
}

void TouchButtonController::DispatchEvent(int button_idx,
                                          TouchButtonEvent event,
                                          uint32_t value,
                                          uint32_t baseline,
                                          uint32_t abs_diff) {
    portENTER_CRITICAL(&snapshot_mux_);
    UpdateSnapshotLocked(button_idx, event, value, baseline, abs_diff);
    portEXIT_CRITICAL(&snapshot_mux_);

    if (!event_callback_) {
        return;
    }
    event_callback_(button_idx + 1, event, value, baseline, abs_diff);
}

uint8_t TouchButtonController::GetPressedMask() const {
    uint8_t mask = 0;
    portENTER_CRITICAL(&snapshot_mux_);
    for (int i = 0; i < 3; i++) {
        if (snapshot_[i].pressed) {
            mask |= static_cast<uint8_t>(1u << i);
        }
    }
    portEXIT_CRITICAL(&snapshot_mux_);
    return mask;
}

bool TouchButtonController::GetButtonState(int button_id, TouchButtonState* out) const {
    if (!out || button_id < 1 || button_id > 3) {
        return false;
    }
    portENTER_CRITICAL(&snapshot_mux_);
    *out = snapshot_[button_id - 1];
    portEXIT_CRITICAL(&snapshot_mux_);
    return true;
}

TouchThresholds TouchButtonController::CopyThresholds() const {
    portENTER_CRITICAL(&snapshot_mux_);
    TouchThresholds t = thresholds_;
    portEXIT_CRITICAL(&snapshot_mux_);
    return t;
}

TouchThresholds TouchButtonController::GetThresholds() const {
    return CopyThresholds();
}

void TouchButtonController::ApplyThresholdsLocked(const TouchThresholds& thr) {
    thresholds_ = thr;
}

bool TouchButtonController::PersistThresholdsUnlocked(const TouchThresholds& thr) {
    nvs_handle_t h;
    if (nvs_open(DEEP_DOG_TOUCH_THR_NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open %s failed", DEEP_DOG_TOUCH_THR_NVS_NS);
        return false;
    }
    const TouchThrNvsBlob blob = ToBlob(thr);
    esp_err_t err = nvs_set_blob(h, DEEP_DOG_TOUCH_THR_NVS_KEY, &blob, sizeof(blob));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs save thresholds failed: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "thresholds saved to NVS");
    return true;
}

bool TouchButtonController::LoadThresholdsFromNvs() {
    nvs_handle_t h;
    if (nvs_open(DEEP_DOG_TOUCH_THR_NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    TouchThrNvsBlob blob{};
    size_t len = sizeof(blob);
    esp_err_t err = nvs_get_blob(h, DEEP_DOG_TOUCH_THR_NVS_KEY, &blob, &len);
    nvs_close(h);
    if (err != ESP_OK || len != sizeof(blob)) {
        return false;
    }
    TouchThresholds t;
    if (!FromBlob(blob, &t)) {
        return false;
    }
    SanitizeThresholds(&t);
    portENTER_CRITICAL(&snapshot_mux_);
    ApplyThresholdsLocked(t);
    portEXIT_CRITICAL(&snapshot_mux_);
    ESP_LOGI(TAG, "thresholds loaded from NVS (btn3 press_min=%u)", (unsigned)t.press_abs_min[2]);
    return true;
}

bool TouchButtonController::SetThresholds(const TouchThresholds& thr, bool persist) {
    TouchThresholds t = thr;
    SanitizeThresholds(&t);
    portENTER_CRITICAL(&snapshot_mux_);
    ApplyThresholdsLocked(t);
    portEXIT_CRITICAL(&snapshot_mux_);
    if (persist) {
        return PersistThresholdsUnlocked(t);
    }
    return true;
}

bool TouchButtonController::ResetThresholdsToFactory(bool persist) {
    return SetThresholds(FactoryThresholds(), persist);
}

bool TouchButtonController::WantMqttDebug() const {
    if (mqtt_debug_ || DEEP_DOG_TOUCH_MQTT_DEBUG) {
        return true;
    }
    portENTER_CRITICAL(&snapshot_mux_);
    const bool active = calib_active_;
    portEXIT_CRITICAL(&snapshot_mux_);
    return active;
}

void TouchButtonController::FlushPendingShortPress(int button_idx,
                                                   uint32_t value,
                                                   uint32_t baseline,
                                                   uint32_t abs_diff) {
    if (!pending_short_[button_idx]) {
        return;
    }
    pending_short_[button_idx] = false;
    pending_short_deadline_us_[button_idx] = 0;
    click_armed_[button_idx] = false;
    DispatchEvent(button_idx, TouchButtonEvent::kShortPress, value, baseline, abs_diff);
}

void TouchButtonController::PollDoubleClickWindows() {
    const int64_t now = esp_timer_get_time();
    for (int i = 0; i < 3; i++) {
        if (!pending_short_[i]) {
            continue;
        }
        if (now < pending_short_deadline_us_[i]) {
            continue;
        }
        FlushPendingShortPress(i,
                               pending_short_value_[i],
                               pending_short_baseline_[i],
                               pending_short_abs_diff_[i]);
    }
}

void TouchButtonController::FailCalibrate(const char* err) {
    // Caller must hold snapshot_mux_ (or be sole owner). No logging here.
    calib_active_ = false;
    calib_phase_ = TouchCalibPhase::kFail;
    calib_error_ = err ? err : "fail";
    calib_in_peak_ = false;
}

bool TouchButtonController::StartCalibrate(int button_id, int samples) {
    if (button_id < 1 || button_id > 3) {
        return false;
    }
    if (samples < 1) {
        samples = 1;
    }
    if (samples > DEEP_DOG_TOUCH_CALIB_MAX_SAMPLES) {
        samples = DEEP_DOG_TOUCH_CALIB_MAX_SAMPLES;
    }
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&snapshot_mux_);
    calib_active_ = true;
    calib_button_idx_ = button_id - 1;
    calib_phase_ = TouchCalibPhase::kIdle;
    calib_count_ = 0;
    calib_samples_ = samples;
    calib_deadline_us_ = now + (int64_t)DEEP_DOG_TOUCH_CALIB_TIMEOUT_MS * 1000;
    calib_idle_until_us_ = now + (int64_t)DEEP_DOG_TOUCH_CALIB_IDLE_MS * 1000;
    calib_idle_peak_ = 0;
    calib_soft_thr_ = 80;
    calib_accept_min_ = DEEP_DOG_TOUCH_CALIB_PEAK_ABS_MIN;
    calib_in_peak_ = false;
    calib_peak_cur_ = 0;
    calib_refractory_until_us_ = 0;
    calib_error_ = nullptr;
    for (int i = 0; i < DEEP_DOG_TOUCH_CALIB_MAX_SAMPLES; i++) {
        calib_peaks_[i] = 0;
    }
    portEXIT_CRITICAL(&snapshot_mux_);
    ESP_LOGI(TAG, "calib start btn=%d samples=%d", button_id, samples);
    return true;
}

void TouchButtonController::CancelCalibrate() {
    bool was_active = false;
    portENTER_CRITICAL(&snapshot_mux_);
    if (calib_active_) {
        FailCalibrate("cancelled");
        was_active = true;
    }
    portEXIT_CRITICAL(&snapshot_mux_);
    if (was_active) {
        ESP_LOGW(TAG, "calib fail: cancelled");
    }
}

TouchCalibStatus TouchButtonController::GetCalibStatus() const {
    TouchCalibStatus st;
    portENTER_CRITICAL(&snapshot_mux_);
    st.active = calib_active_;
    st.button_id = calib_button_idx_ >= 0 ? calib_button_idx_ + 1 : 0;
    st.phase = calib_phase_;
    st.count = calib_count_;
    st.samples = calib_samples_;
    st.error = calib_error_;
    if (!calib_active_ && (calib_phase_ == TouchCalibPhase::kDone || calib_phase_ == TouchCalibPhase::kFail)) {
        st.active = false;
    }
    portEXIT_CRITICAL(&snapshot_mux_);
    return st;
}

void TouchButtonController::TickCalibrate(int button_idx, uint32_t abs_diff) {
    // Defer all ESP_LOG* until after portEXIT_CRITICAL (printf takes locks).
    bool log_collect = false;
    uint32_t log_soft = 0;
    uint32_t log_accept = 0;
    uint32_t log_idle_peak = 0;
    bool log_peak = false;
    int log_peak_idx = 0;
    uint32_t log_peak_val = 0;
    bool log_reject = false;
    uint32_t log_reject_val = 0;
    bool log_done = false;
    int log_done_btn = 0;
    uint32_t log_press = 0;
    uint32_t log_release = 0;
    uint32_t log_median = 0;
    bool finished = false;
    TouchThresholds persist_copy{};

    portENTER_CRITICAL(&snapshot_mux_);
    if (!calib_active_ || button_idx != calib_button_idx_) {
        portEXIT_CRITICAL(&snapshot_mux_);
        return;
    }
    const int64_t now = esp_timer_get_time();
    if (now > calib_deadline_us_) {
        FailCalibrate("timeout");
        portEXIT_CRITICAL(&snapshot_mux_);
        ESP_LOGW(TAG, "calib fail: timeout");
        return;
    }

    if (calib_phase_ == TouchCalibPhase::kIdle) {
        if (abs_diff > calib_idle_peak_) {
            calib_idle_peak_ = abs_diff;
        }
        if (now >= calib_idle_until_us_) {
            uint32_t soft = calib_idle_peak_ * 3;
            if (soft < DEEP_DOG_TOUCH_CALIB_SOFT_FLOOR) {
                soft = DEEP_DOG_TOUCH_CALIB_SOFT_FLOOR;
            }
            calib_soft_thr_ = soft;
            uint32_t accept = soft * DEEP_DOG_TOUCH_CALIB_PEAK_SOFT_MULT;
            if (accept < DEEP_DOG_TOUCH_CALIB_PEAK_ABS_MIN) {
                accept = DEEP_DOG_TOUCH_CALIB_PEAK_ABS_MIN;
            }
            calib_accept_min_ = accept;
            calib_phase_ = TouchCalibPhase::kCollect;
            calib_in_peak_ = false;
            calib_refractory_until_us_ = 0;
            log_collect = true;
            log_soft = calib_soft_thr_;
            log_accept = calib_accept_min_;
            log_idle_peak = calib_idle_peak_;
        }
        portEXIT_CRITICAL(&snapshot_mux_);
        if (log_collect) {
            ESP_LOGI(TAG, "calib collect soft_thr=%u accept_min=%u idle_peak=%u",
                     (unsigned)log_soft, (unsigned)log_accept, (unsigned)log_idle_peak);
        }
        return;
    }

    if (calib_phase_ != TouchCalibPhase::kCollect) {
        portEXIT_CRITICAL(&snapshot_mux_);
        return;
    }

    if (now < calib_refractory_until_us_) {
        // cooling after a counted peak; ignore bounce
        calib_in_peak_ = false;
        calib_peak_cur_ = 0;
        portEXIT_CRITICAL(&snapshot_mux_);
        return;
    }

    if (!calib_in_peak_) {
        if (abs_diff > calib_soft_thr_) {
            calib_in_peak_ = true;
            calib_peak_cur_ = abs_diff;
        }
    } else {
        if (abs_diff > calib_peak_cur_) {
            calib_peak_cur_ = abs_diff;
        }
        if (abs_diff < calib_soft_thr_) {
            const uint32_t peak = calib_peak_cur_;
            calib_in_peak_ = false;
            calib_peak_cur_ = 0;
            if (peak < calib_accept_min_) {
                log_reject = true;
                log_reject_val = peak;
            } else if (calib_count_ < calib_samples_ && calib_count_ < DEEP_DOG_TOUCH_CALIB_MAX_SAMPLES) {
                calib_peaks_[calib_count_++] = peak;
                calib_refractory_until_us_ =
                    now + (int64_t)DEEP_DOG_TOUCH_CALIB_REFRACTORY_MS * 1000;
                log_peak = true;
                log_peak_idx = calib_count_;
                log_peak_val = peak;
            }
            if (calib_count_ >= calib_samples_) {
                const int idx = calib_button_idx_;
                // median of peaks (sort copy)
                uint32_t sorted[DEEP_DOG_TOUCH_CALIB_MAX_SAMPLES];
                const int n = calib_count_;
                for (int i = 0; i < n; i++) {
                    sorted[i] = calib_peaks_[i];
                }
                for (int i = 0; i < n; i++) {
                    for (int j = i + 1; j < n; j++) {
                        if (sorted[j] < sorted[i]) {
                            const uint32_t tmp = sorted[i];
                            sorted[i] = sorted[j];
                            sorted[j] = tmp;
                        }
                    }
                }
                const uint32_t median = sorted[n / 2];
                uint32_t press_min = static_cast<uint32_t>(median * 0.40f + 0.5f);
                if (press_min < DEEP_DOG_TOUCH_CALIB_RESULT_FLOOR) {
                    press_min = DEEP_DOG_TOUCH_CALIB_RESULT_FLOOR;
                }
                if (median > 100 && press_min + 100 > median) {
                    press_min = median - 100;
                }
                uint32_t release_min = (press_min * 3) / 4;
                if (release_min < 100) {
                    release_min = 100;
                }
                thresholds_.press_abs_min[idx] = press_min;
                thresholds_.release_abs_min[idx] = release_min;
                SanitizeThresholds(&thresholds_);
                persist_copy = thresholds_;
                calib_active_ = false;
                calib_phase_ = TouchCalibPhase::kDone;
                calib_error_ = nullptr;
                calib_in_peak_ = false;
                finished = true;
                log_done = true;
                log_done_btn = idx + 1;
                log_press = press_min;
                log_release = release_min;
                log_median = median;
            }
        }
    }
    portEXIT_CRITICAL(&snapshot_mux_);

    if (log_reject) {
        ESP_LOGD(TAG, "calib reject peak=%u (below accept_min)", (unsigned)log_reject_val);
    }
    if (log_peak) {
        ESP_LOGI(TAG, "calib peak[%d]=%u", log_peak_idx, (unsigned)log_peak_val);
    }
    if (log_done) {
        ESP_LOGI(TAG, "calib done btn=%d press_min=%u release_min=%u median_peak=%u", log_done_btn,
                 (unsigned)log_press, (unsigned)log_release, (unsigned)log_median);
    }
    if (finished) {
        PersistThresholdsUnlocked(persist_copy);
    }
}

bool TouchButtonController::Initialize(gpio_num_t button1_gpio,
                                       gpio_num_t button2_gpio,
                                       gpio_num_t button3_gpio,
                                       TouchButtonEventCallback callback) {
    event_callback_ = std::move(callback);
    thresholds_ = FactoryThresholds();

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
        pending_short_[i] = false;
        pending_short_deadline_us_[i] = 0;
        click_armed_[i] = false;
        snapshot_[i] = TouchButtonState{};
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

    const uint16_t long_press_cycles = static_cast<uint16_t>(
        (DEEP_DOG_TOUCH_LONG_PRESS_MS + DEEP_DOG_TOUCH_POLL_MS - 1) / DEEP_DOG_TOUCH_POLL_MS);
    const int64_t double_window_us = (int64_t)DEEP_DOG_TOUCH_DOUBLE_MS * 1000;

    while (true) {
        self->PollDoubleClickWindows();

        TouchThresholds thr = self->CopyThresholds();

        for (int i = 0; i < 3; i++) {
            uint32_t value = 0;
            (void)touch_pad_read_raw_data(self->touch_pads_[i], &value);
            const uint32_t baseline = self->touch_baseline_[i];
            if (baseline == 0) {
                continue;
            }

            const int64_t diff = (int64_t)value - (int64_t)baseline;
            const uint32_t abs_diff = (uint32_t)(diff >= 0 ? diff : -diff);

            // Always refresh raw into snapshot for MQTT debug / calib UI
            portENTER_CRITICAL(&self->snapshot_mux_);
            self->snapshot_[i].value = value;
            self->snapshot_[i].baseline = baseline;
            self->snapshot_[i].abs_diff = abs_diff;
            portEXIT_CRITICAL(&self->snapshot_mux_);

            self->TickCalibrate(i, abs_diff);

            const uint32_t press_abs = std::max(
                (uint32_t)(baseline * thr.press_abs_ratio) + thr.press_abs_offset,
                thr.press_abs_min[i]);
            const uint32_t release_abs = std::max(
                (uint32_t)(baseline * thr.release_abs_ratio) + thr.release_abs_offset,
                thr.release_abs_min[i]);
            const uint8_t confirm_cycles = thr.debounce_cycles;

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
                    (uint32_t)((float)baseline * thr.baseline_update_abs_ratio) +
                    thr.baseline_update_abs_offset;
                if (abs_diff < baseline_update_abs) {
                    self->touch_baseline_[i] = (uint32_t)(
                        (1.0f - thr.baseline_alpha) * (float)baseline + thr.baseline_alpha * (float)value);
                }
            } else {
                if (abs_diff < release_abs) {
                    if (self->touch_release_confirm_cnt_[i] < 255) {
                        self->touch_release_confirm_cnt_[i]++;
                    }
                    if (self->touch_release_confirm_cnt_[i] >= confirm_cycles) {
                        const bool was_long = self->touch_longpress_fired_[i];
                        self->touch_pressed_[i] = false;
                        self->touch_release_confirm_cnt_[i] = 0;
                        self->touch_hold_cycles_[i] = 0;
                        self->touch_longpress_fired_[i] = false;
                        self->DispatchEvent(i, TouchButtonEvent::kRelease, value, baseline, abs_diff);

                        if (was_long) {
                            self->pending_short_[i] = false;
                            self->click_armed_[i] = false;
                        } else if (self->pending_short_[i] || self->click_armed_[i]) {
                            self->pending_short_[i] = false;
                            self->pending_short_deadline_us_[i] = 0;
                            self->click_armed_[i] = false;
                            self->DispatchEvent(i, TouchButtonEvent::kDoubleClick, value, baseline, abs_diff);
                        } else {
                            self->pending_short_[i] = true;
                            self->pending_short_deadline_us_[i] =
                                esp_timer_get_time() + double_window_us;
                            self->pending_short_value_[i] = value;
                            self->pending_short_baseline_[i] = baseline;
                            self->pending_short_abs_diff_[i] = abs_diff;
                            self->click_armed_[i] = true;
                        }
                    }
                } else {
                    self->touch_release_confirm_cnt_[i] = 0;
                    if (self->touch_hold_cycles_[i] < 0xFFFF) {
                        self->touch_hold_cycles_[i]++;
                    }
                    if (!self->touch_longpress_fired_[i] &&
                        self->touch_hold_cycles_[i] >= long_press_cycles) {
                        self->touch_longpress_fired_[i] = true;
                        self->pending_short_[i] = false;
                        self->click_armed_[i] = false;
                        self->DispatchEvent(i, TouchButtonEvent::kLongPress, value, baseline, abs_diff);
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(DEEP_DOG_TOUCH_POLL_MS));
    }
}
