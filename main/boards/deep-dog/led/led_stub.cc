#include "led/led_stub.h"

#include <esp_log.h>

#define TAG "dog_led"

#if DEEP_DOG_LED_ENABLE

void DeepDogLedInit(void) {
    if (DEEP_DOG_LED_STRIP_GPIO == GPIO_NUM_NC || DEEP_DOG_LED_STRIP_COUNT <= 0) {
        ESP_LOGW(TAG, "LED enabled but GPIO/count unset — skip");
        return;
    }
    ESP_LOGI(TAG, "LED placeholder gpio=%d count=%d (wire CircularStrip later)",
             (int)DEEP_DOG_LED_STRIP_GPIO, DEEP_DOG_LED_STRIP_COUNT);
}

#else

void DeepDogLedInit(void) {}

#endif
