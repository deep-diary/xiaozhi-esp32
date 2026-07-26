#include "rs485/rs485_config.h"
#include <esp_log.h>
#define TAG "dog_rs485"
#if DEEP_DOG_RS485_ENABLE
void DeepDogRs485Init(void) {
    ESP_LOGI(TAG, "RS485 placeholder TX=%d RX=%d", (int)DEEP_DOG_RS485_TX_GPIO, (int)DEEP_DOG_RS485_RX_GPIO);
}
#else
void DeepDogRs485Init(void) {}
#endif
