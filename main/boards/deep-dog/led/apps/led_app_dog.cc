#include "led/apps/led_app_dog.h"

#include "led/led_strip_control.h"
#include "config.h"

#include <esp_log.h>

#define TAG "led_app_dog"

#if DEEP_DOG_LED_ENABLE

void LedAppDogInit(LedStripControl* ctrl) {
    (void)ctrl;
    ESP_LOGI(TAG, "dog LED app registered (active when mode=5)");
}

void LedAppDogOnEnterSystem(LedStripControl* ctrl) {
    if (!ctrl || !ctrl->ok()) {
        return;
    }
    /* 默认空闲：绿色慢呼吸；业务语义不上报 MQTT，仅驱动灯效 */
    const StripColor low = {0, 8, 0};
    const StripColor high = {0, 64, 0};
    if (ctrl->strip()) {
        ctrl->strip()->Breathe(low, high, 50);
    }
    ctrl->UpdateState(DEEP_DOG_LED_MODE_SYSTEM, high, low, 50, 3);
    ESP_LOGI(TAG, "system bind: idle green breathe");
}

#else

void LedAppDogInit(LedStripControl*) {}
void LedAppDogOnEnterSystem(LedStripControl*) {}

#endif
