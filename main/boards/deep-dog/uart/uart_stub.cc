#include "uart/uart_config.h"

#include <esp_log.h>

#define TAG "dog_uart"

#if DEEP_DOG_UART_ENABLE

void DeepDogUartInit(void) {
    ESP_LOGI(TAG, "UART placeholder init TX=%d RX=%d baud=%d",
             (int)UART_ECHO_TXD, (int)UART_ECHO_RXD, (int)ECHO_UART_BAUD_RATE);
    /* TODO: uart_driver_install / uart_set_pin */
}

#else

void DeepDogUartInit(void) {}

#endif
