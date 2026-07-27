#pragma once

#include "touch_btn/touch_app.h"
#include "touch_btn/touch_config.h"
#include "touch_btn/touch_event_hub.h"

#if DEEP_DOG_TOUCH_COMBO_ENABLE
#include "touch_btn/touch_combo_recognizer.h"
#endif

#include <esp_timer.h>

#include <functional>

/** 从 Hub 出队并 fan-out 到已注册应用；由板级 esp_timer 周期 Poll */
class TouchAppDispatcher {
public:
    static constexpr int kMaxApps = 8;
#if DEEP_DOG_TOUCH_COMBO_ENABLE
    using ComboHitListener = std::function<void(const char* combo_id)>;
#endif

    explicit TouchAppDispatcher(TouchEventHub* hub);
    ~TouchAppDispatcher();

    bool Register(ITouchApp* app);
#if DEEP_DOG_TOUCH_COMBO_ENABLE
    void SetComboRecognizer(TouchComboRecognizer* combo);
    void SetComboHitListener(ComboHitListener listener);
#endif
    bool StartPeriodic(uint64_t interval_us);
    void Stop();
    void Poll();

private:
    static void TimerCb(void* arg);

    TouchEventHub* hub_ = nullptr;
    ITouchApp* apps_[kMaxApps] = {};
    int app_count_ = 0;
    esp_timer_handle_t timer_ = nullptr;
#if DEEP_DOG_TOUCH_COMBO_ENABLE
    TouchComboRecognizer* combo_ = nullptr;
    ComboHitListener combo_hit_listener_;
#endif
};
