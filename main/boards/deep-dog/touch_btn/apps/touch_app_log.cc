#include "touch_btn/apps/touch_app_log.h"

#include <esp_log.h>

#define TAG "touch_app_log"

namespace {

const char* EventName(TouchButtonEvent e) {
    switch (e) {
        case TouchButtonEvent::kPress:
            return "press";
        case TouchButtonEvent::kRelease:
            return "release";
        case TouchButtonEvent::kLongPress:
            return "long_press";
        case TouchButtonEvent::kShortPress:
            return "short_press";
        case TouchButtonEvent::kDoubleClick:
            return "double_click";
    }
    return "?";
}

}  // namespace

void TouchAppLog::OnEvent(const TouchEvent& ev) {
    ESP_LOGI(TAG,
             "btn=%d event=%s mask=0x%02x value=%u baseline=%u abs_diff=%u",
             ev.button_id,
             EventName(ev.event),
             (unsigned)ev.pressed_mask,
             (unsigned)ev.value,
             (unsigned)ev.baseline,
             (unsigned)ev.abs_diff);
}
