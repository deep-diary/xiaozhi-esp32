#include "handle/handle_event_hub.h"

#include <esp_log.h>
#include <esp_timer.h>

#define TAG "handle_hub"

HandleEventHub::HandleEventHub() = default;

HandleEventHub::~HandleEventHub() {
    if (queue_) {
        vQueueDelete(queue_);
        queue_ = nullptr;
    }
}

bool HandleEventHub::Init(UBaseType_t depth) {
    if (queue_) {
        return true;
    }
    queue_ = xQueueCreate(depth, sizeof(HandleSnapshot));
    if (!queue_) {
        ESP_LOGE(TAG, "queue create failed");
        return false;
    }
    return true;
}

void HandleEventHub::SetPushListener(PushListener listener) {
    listener_ = std::move(listener);
}

bool HandleEventHub::Push(const HandleSnapshot& snap) {
    if (!queue_) {
        return false;
    }

    HandleSnapshot stored = snap;
    if (stored.ts_us == 0) {
        stored.ts_us = esp_timer_get_time();
    }

    portENTER_CRITICAL(&mux_);
    snapshot_ = stored;
    portEXIT_CRITICAL(&mux_);

    if (xQueueSend(queue_, &stored, 0) != pdTRUE) {
        HandleSnapshot drop;
        xQueueReceive(queue_, &drop, 0);
        if (xQueueSend(queue_, &stored, 0) != pdTRUE) {
            ESP_LOGW(TAG, "queue full, drop snapshot");
        }
    }

    if (listener_) {
        listener_(stored);
    }
    return true;
}

bool HandleEventHub::Pop(HandleSnapshot* out, TickType_t wait) {
    if (!queue_ || !out) {
        return false;
    }
    return xQueueReceive(queue_, out, wait) == pdTRUE;
}

HandleSnapshot HandleEventHub::GetSnapshot() const {
    portENTER_CRITICAL(&mux_);
    const HandleSnapshot s = snapshot_;
    portEXIT_CRITICAL(&mux_);
    return s;
}

void HandleEventHub::SetAppsEnabled(bool enabled) {
    portENTER_CRITICAL(&mux_);
    apps_enabled_ = enabled;
    portEXIT_CRITICAL(&mux_);
}

bool HandleEventHub::AppsEnabled() const {
    portENTER_CRITICAL(&mux_);
    const bool e = apps_enabled_;
    portEXIT_CRITICAL(&mux_);
    return e;
}
