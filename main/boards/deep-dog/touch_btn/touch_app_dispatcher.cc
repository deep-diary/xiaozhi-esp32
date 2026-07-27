#include "touch_btn/touch_app_dispatcher.h"

#include <esp_log.h>

#define TAG "touch_disp"

TouchAppDispatcher::TouchAppDispatcher(TouchEventHub* hub) : hub_(hub) {}

TouchAppDispatcher::~TouchAppDispatcher() {
    Stop();
    if (timer_) {
        esp_timer_delete(timer_);
        timer_ = nullptr;
    }
}

bool TouchAppDispatcher::Register(ITouchApp* app) {
    if (!app || app_count_ >= kMaxApps) {
        return false;
    }
    apps_[app_count_++] = app;
    ESP_LOGI(TAG, "registered touch app: %s", app->Name());
    return true;
}

#if DEEP_DOG_TOUCH_COMBO_ENABLE
void TouchAppDispatcher::SetComboRecognizer(TouchComboRecognizer* combo) {
    combo_ = combo;
    if (combo_) {
        ESP_LOGI(TAG, "touch combo recognizer attached (consume=%d)", DEEP_DOG_TOUCH_COMBO_CONSUME);
    }
}

void TouchAppDispatcher::SetComboHitListener(ComboHitListener listener) {
    combo_hit_listener_ = std::move(listener);
}
#endif

void TouchAppDispatcher::TimerCb(void* arg) {
    auto* self = static_cast<TouchAppDispatcher*>(arg);
    if (self) {
        self->Poll();
    }
}

bool TouchAppDispatcher::StartPeriodic(uint64_t interval_us) {
    if (!hub_) {
        return false;
    }
    if (!timer_) {
        esp_timer_create_args_t args = {
            .callback = &TouchAppDispatcher::TimerCb,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "touch_disp",
            .skip_unhandled_events = true,
        };
        if (esp_timer_create(&args, &timer_) != ESP_OK) {
            ESP_LOGE(TAG, "esp_timer_create failed");
            return false;
        }
    }
    esp_timer_stop(timer_);
    const esp_err_t err = esp_timer_start_periodic(timer_, interval_us);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_start_periodic: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

void TouchAppDispatcher::Stop() {
    if (timer_) {
        esp_timer_stop(timer_);
    }
}

void TouchAppDispatcher::Poll() {
    if (!hub_) {
        return;
    }
    TouchEvent ev;
    while (hub_->Pop(&ev, 0)) {
        bool skip_apps = false;
#if DEEP_DOG_TOUCH_COMBO_ENABLE
        if (combo_) {
            const char* combo_id = combo_->Feed(ev);
            if (combo_id != nullptr) {
                if (combo_hit_listener_) {
                    combo_hit_listener_(combo_id);
                }
                if (DEEP_DOG_TOUCH_COMBO_CONSUME) {
                    skip_apps = true;
                }
            }
        }
#endif
        if (skip_apps) {
            continue;
        }
        for (int i = 0; i < app_count_; i++) {
            if (apps_[i]) {
                apps_[i]->OnEvent(ev);
            }
        }
    }
}
