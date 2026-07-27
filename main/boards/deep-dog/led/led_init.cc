#include "led/led_init.h"

#include "led/led_config.h"
#include "led/led_strip_control.h"
#include "led/apps/led_app_dog.h"
#include "led/circular_strip.h"

#include <esp_log.h>

#define TAG "dog_led"

#if DEEP_DOG_LED_ENABLE

namespace {

CircularStrip* g_strip = nullptr;
LedStripControl* g_control = nullptr;

}  // namespace

void DeepDogLedInit(void) {
    if (DEEP_DOG_LED_STRIP_GPIO == GPIO_NUM_NC || DEEP_DOG_LED_STRIP_COUNT <= 0) {
        ESP_LOGW(TAG, "LED enabled but GPIO/count unset — skip");
        return;
    }
    if (g_control) {
        return;
    }

    g_strip = new CircularStrip(DEEP_DOG_LED_STRIP_GPIO, static_cast<uint16_t>(DEEP_DOG_LED_STRIP_COUNT));
    g_control = new LedStripControl(g_strip, DEEP_DOG_LED_STRIP_COUNT);
    ESP_LOGI(TAG, "WS2812 init gpio=%d count=%d", (int)DEEP_DOG_LED_STRIP_GPIO, DEEP_DOG_LED_STRIP_COUNT);

    LedAppDogInit(g_control);
    /* 默认交还应用绑定（空闲绿呼吸） */
    LedAppDogOnEnterSystem(g_control);
}

LedStripControl* DeepDogLedGetControl(void) {
    return g_control;
}

CircularStrip* DeepDogLedGetStrip(void) {
    return g_strip;
}

#else

void DeepDogLedInit(void) {}

LedStripControl* DeepDogLedGetControl(void) {
    return nullptr;
}

CircularStrip* DeepDogLedGetStrip(void) {
    return nullptr;
}

#endif
