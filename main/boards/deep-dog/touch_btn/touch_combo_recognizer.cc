#include "touch_btn/touch_combo_recognizer.h"

#include <esp_log.h>
#include <esp_timer.h>

#include <ctime>

#define TAG "touch_combo"

namespace {

constexpr int64_t LongPressUs() {
    return static_cast<int64_t>(DEEP_DOG_TOUCH_LONG_PRESS_MS) * 1000;
}

/** pair: 0 → bits 0|1 (keys 1+2), 1 → 0|2 (1+3), 2 → 1|2 (2+3) */
constexpr uint8_t kPairMask[3] = {0x3, 0x5, 0x6};

const char* kChordId[3] = {
    "chord_short_1_2",
    "chord_short_1_3",
    "chord_short_2_3",
};

int64_t UnixTsSec() {
    const time_t now = time(nullptr);
    if (now > 1000000000) {
        return static_cast<int64_t>(now);
    }
    return static_cast<int64_t>(esp_timer_get_time() / 1000000LL);
}

}  // namespace

TouchComboRecognizer::TouchComboRecognizer() {
    Reset();
}

void TouchComboRecognizer::Reset() {
    down_mask_ = 0;
    for (int i = 0; i < 3; i++) {
        down_ts_us_[i] = 0;
        overlap_start_us_[i] = 0;
        overlap_armed_[i] = false;
        hold_tap_fired_for_[i] = false;
        suppress_chord_for_btn_[i] = false;
    }
}

void TouchComboRecognizer::NoteHit(const char* id) {
    last_combo_id_ = id;
    last_combo_ts_sec_ = UnixTsSec();
    ESP_LOGI(TAG, "combo %s", id ? id : "?");
}

const char* TouchComboRecognizer::Feed(const TouchEvent& ev) {
    if (ev.button_id < 1 || ev.button_id > 3) {
        return nullptr;
    }
    const int64_t ts = ev.ts_us != 0 ? ev.ts_us : esp_timer_get_time();
    const uint8_t mask = ev.pressed_mask;

    switch (ev.event) {
        case TouchButtonEvent::kPress:
            OnPress(ev.button_id, ts, mask);
            // hold+tap：键1 已按下时，2/3 的 press
            if ((ev.button_id == 2 || ev.button_id == 3) && (down_mask_ & Bit(1))) {
                const int64_t t1 = down_ts_us_[0];
                const int64_t tb = down_ts_us_[ev.button_id - 1];
                constexpr int64_t kHoldLeadUs = 80 * 1000;  // 80ms
                if (t1 > 0 && tb > 0 && (tb - t1) >= kHoldLeadUs) {
                    if (!hold_tap_fired_for_[ev.button_id - 1]) {
                        hold_tap_fired_for_[ev.button_id - 1] = true;
                        suppress_chord_for_btn_[ev.button_id - 1] = true;
                        const char* id = (ev.button_id == 2) ? "hold1_tap2" : "hold1_tap3";
                        NoteHit(id);
                        return id;
                    }
                }
            }
            return nullptr;

        case TouchButtonEvent::kRelease: {
            const char* hit = OnRelease(ev.button_id, ts, mask);
            if (hit) {
                NoteHit(hit);
            }
            return hit;
        }

        default:
            down_mask_ = mask;
            return nullptr;
    }
}

void TouchComboRecognizer::OnPress(int button_id, int64_t ts_us, uint8_t mask) {
    const uint8_t bit = Bit(button_id);
    const uint8_t prev = down_mask_;
    down_mask_ = mask | bit;
    down_ts_us_[button_id - 1] = ts_us;
    hold_tap_fired_for_[button_id - 1] = false;
    suppress_chord_for_btn_[button_id - 1] = false;

    for (int p = 0; p < 3; p++) {
        const uint8_t pm = kPairMask[p];
        if ((down_mask_ & pm) == pm && (prev & pm) != pm) {
            overlap_start_us_[p] = ts_us;
            overlap_armed_[p] = true;
        }
    }
}

const char* TouchComboRecognizer::OnRelease(int button_id, int64_t ts_us, uint8_t mask) {
    const uint8_t bit = Bit(button_id);
    down_mask_ = mask & static_cast<uint8_t>(~bit);

    const char* hit = nullptr;
    for (int p = 0; p < 3; p++) {
        if (!overlap_armed_[p]) {
            continue;
        }
        const uint8_t pm = kPairMask[p];
        if ((pm & bit) == 0) {
            continue;
        }
        if ((down_mask_ & pm) != 0) {
            continue;
        }
        if (overlap_start_us_[p] <= 0) {
            overlap_armed_[p] = false;
            continue;
        }
        const int64_t dur = ts_us - overlap_start_us_[p];
        overlap_armed_[p] = false;
        overlap_start_us_[p] = 0;

        bool suppressed = false;
        for (int b = 0; b < 3; b++) {
            if ((pm & (1u << b)) && suppress_chord_for_btn_[b]) {
                suppressed = true;
            }
        }
        if (suppressed) {
            continue;
        }
        if (dur > 0 && dur < LongPressUs()) {
            hit = kChordId[p];
            ESP_LOGD(TAG, "chord candidate %s dur_ms=%d", hit, static_cast<int>(dur / 1000));
            break;
        }
    }

    if ((down_mask_ & Bit(1)) == 0) {
        hold_tap_fired_for_[1] = false;
        hold_tap_fired_for_[2] = false;
        suppress_chord_for_btn_[1] = false;
        suppress_chord_for_btn_[2] = false;
    }
    down_ts_us_[button_id - 1] = 0;
    return hit;
}
