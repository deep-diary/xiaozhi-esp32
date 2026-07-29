#include "handle/handle_app_dispatcher.h"

#include <esp_log.h>

#define TAG "handle_disp"

HandleAppDispatcher::HandleAppDispatcher(HandleEventHub* hub) : hub_(hub) {}

HandleAppDispatcher::~HandleAppDispatcher() {
    Stop();
    if (timer_) {
        esp_timer_delete(timer_);
        timer_ = nullptr;
    }
}

bool HandleAppDispatcher::Register(IHandleApp* app) {
    if (!app || app_count_ >= kMaxApps) {
        return false;
    }
    apps_[app_count_++] = app;
    ESP_LOGI(TAG, "registered handle app: %s", app->Name());
    return true;
}

void HandleAppDispatcher::TimerCb(void* arg) {
    auto* self = static_cast<HandleAppDispatcher*>(arg);
    if (self) {
        self->Poll();
    }
}

bool HandleAppDispatcher::StartPeriodic(uint64_t interval_us) {
    if (!hub_) {
        return false;
    }
    if (!timer_) {
        esp_timer_create_args_t args = {
            .callback = &HandleAppDispatcher::TimerCb,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "handle_disp",
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

void HandleAppDispatcher::Stop() {
    if (timer_) {
        esp_timer_stop(timer_);
    }
}

void HandleAppDispatcher::Poll() {
    if (!hub_) {
        return;
    }
    HandleSnapshot snap;
    while (hub_->Pop(&snap, 0)) {
        for (int i = 0; i < app_count_; i++) {
            if (apps_[i]) {
                apps_[i]->OnSnapshot(snap);
            }
        }
    }
}
