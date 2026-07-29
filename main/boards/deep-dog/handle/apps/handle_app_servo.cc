#include "handle/apps/handle_app_servo.h"

#include <esp_log.h>

#define TAG "handle_app_servo"

void HandleAppServo::OnSnapshot(const HandleSnapshot& snap) {
    if (!snap.connected) {
        return;
    }
    // Placeholder: map right stick later
    ESP_LOGD(TAG, "rx=%.2f ry=%.2f (stub)", snap.axes.rx, snap.axes.ry);
}
