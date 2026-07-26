#ifndef _DEEP_DOG_UART_CONFIG_H_
#define _DEEP_DOG_UART_CONFIG_H_

#include "config.h"
#include <driver/uart.h>

/**
 * 自由引出脚 UART 模式占位（A=TXD B=RXD）。
 * 实现后续按产品协议填充；当前仅配置宏。
 */

#if DEEP_DOG_UART_AVAILABLE
#ifndef UART_ECHO_TXD
#define UART_ECHO_TXD DEEP_DOG_EXT_PIN_A_GPIO
#endif
#ifndef UART_ECHO_RXD
#define UART_ECHO_RXD DEEP_DOG_EXT_PIN_B_GPIO
#endif
#endif

#ifndef UART_ECHO_RTS
#define UART_ECHO_RTS (-1)
#endif
#ifndef UART_ECHO_CTS
#define UART_ECHO_CTS (-1)
#endif

#ifndef ECHO_UART_PORT_NUM
#define ECHO_UART_PORT_NUM      UART_NUM_1
#endif
#ifndef ECHO_UART_BAUD_RATE
#define ECHO_UART_BAUD_RATE     (115200)
#endif
#ifndef BUF_SIZE
#define BUF_SIZE                (1024)
#endif

#endif  // _DEEP_DOG_UART_CONFIG_H_
