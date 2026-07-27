#include "touch_btn/apps/touch_app_servo.h"

#include <esp_log.h>

#define TAG "touch_app_servo"

void TouchAppServo::OnEvent(const TouchEvent& ev) {
#if !DEEP_DOG_TOUCH_APP_SERVO_ENABLE
    (void)ev;
    return;
#else
    switch (ev.event) {
        case TouchButtonEvent::kShortPress:
            ESP_LOGI(TAG, "servo stub: short_press btn=%d（预留微调）", ev.button_id);
            break;
        case TouchButtonEvent::kLongPress:
            if (ev.button_id == 1) {
                ESP_LOGI(TAG, "servo stub: long_press1（预留归中）");
            } else {
                ESP_LOGI(TAG, "servo stub: long_press btn=%d", ev.button_id);
            }
            break;
        case TouchButtonEvent::kDoubleClick:
            ESP_LOGI(TAG, "servo stub: double_click btn=%d（预留）", ev.button_id);
            break;
        default:
            break;
    }
#endif
}
