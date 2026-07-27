#pragma once

#include "touch_btn/touch_config.h"
#include "touch_btn/touch_event_hub.h"

#include <cstdint>

/**
 * 跨键组合识别（与业务应用无关）。
 * 白名单：chord_short_1_2/1_3/2_3、hold1_tap2/3。
 * 仅在 DEEP_DOG_TOUCH_COMBO_ENABLE=1 时由 Dispatcher 调用。
 */
class TouchComboRecognizer {
public:
    TouchComboRecognizer();

    /** @return 命中时返回静态 id 字符串，否则 nullptr */
    const char* Feed(const TouchEvent& ev);

    void Reset();

    /** 最近一次命中的组合（供 MQTT touch/status.last_combo） */
    const char* LastComboId() const { return last_combo_id_; }
    int64_t LastComboTsSec() const { return last_combo_ts_sec_; }

private:
    static constexpr uint8_t Bit(int button_id) {
        return static_cast<uint8_t>(1u << (button_id - 1));
    }

    void NoteHit(const char* id);
    void OnPress(int button_id, int64_t ts_us, uint8_t mask);
    const char* OnRelease(int button_id, int64_t ts_us, uint8_t mask);

    uint8_t down_mask_ = 0;
    int64_t down_ts_us_[3] = {};
    /** 两键重叠开始时刻；pair index: 0=1+2, 1=1+3, 2=2+3 */
    int64_t overlap_start_us_[3] = {};
    bool overlap_armed_[3] = {};
    /** hold+tap 已对某动作键开火，避免同一次按下重复；且抑制该动作键参与 chord 结算 */
    bool hold_tap_fired_for_[3] = {};
    bool suppress_chord_for_btn_[3] = {};

    const char* last_combo_id_ = nullptr;
    int64_t last_combo_ts_sec_ = 0;
};
