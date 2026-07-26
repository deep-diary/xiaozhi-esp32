#include "io_ext/io_ext_config.h"
#include <esp_log.h>
#define TAG "dog_io"
#if DEEP_DOG_IO_ENABLE
void DeepDogIoExtInit(void) {
    ESP_LOGI(TAG, "IO ext placeholder A=%d B=%d", (int)DEEP_DOG_IO_A_GPIO, (int)DEEP_DOG_IO_B_GPIO);
}
#else
void DeepDogIoExtInit(void) {}
#endif
