#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "application.h"

/** 置 1：先全黑检测、不调 nvs、不主循环（与 ref 一致，堆最干净），用于对比 ref；置 0 正常启动 */
#define FACE_DETECT_BLACK_TEST_IN_MAIN 0

#if FACE_DETECT_BLACK_TEST_IN_MAIN
#include "face_detect_core.hpp"
#endif

#define TAG "main"

extern "C" void app_main(void)
{
#if FACE_DETECT_BLACK_TEST_IN_MAIN
    // 先检测、不 nvs：与 ref 一致，app_main 第一件事即全黑检测，堆最干净
    app_ai::CreateFaceDetectorEarly();
    ESP_LOGI(TAG, "Black test done in main (detect first, no nvs, no app loop). Idle.");
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#else
    // Initialize NVS flash for WiFi configuration
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize and run the application
    auto& app = Application::GetInstance();
    app.Initialize();
    app.Run();  // This function runs the main event loop and never returns
#endif
}
