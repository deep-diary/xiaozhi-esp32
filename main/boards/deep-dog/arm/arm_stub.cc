#include "arm/arm_config.h"

#include <esp_log.h>

#define TAG "dog_arm"

#if DEEP_DOG_ARM_ENABLE

void DeepDogArmInit(void) {
    ESP_LOGI(TAG, "Arm placeholder (needs motor stack) — implement later");
}

#else

void DeepDogArmInit(void) {}

#endif
