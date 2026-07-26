#include "ad/ad_config.h"
#include <esp_log.h>
#define TAG "dog_ad"
#if DEEP_DOG_AD_ENABLE
void DeepDogAdInit(void) {
    ESP_LOGI(TAG, "AD placeholder A=%d B=%d", (int)DEEP_DOG_AD_A_GPIO, (int)DEEP_DOG_AD_B_GPIO);
}
#else
void DeepDogAdInit(void) {}
#endif
