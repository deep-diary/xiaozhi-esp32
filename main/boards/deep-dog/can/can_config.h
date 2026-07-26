#ifndef _DEEP_DOG_CAN_CONFIG_H_
#define _DEEP_DOG_CAN_CONFIG_H_

#include "config.h"

/**
 * CAN / TWAI 模块配置。引脚来自板级自由引出脚成对模式（CAN 时 A=TX B=RX）。
 */

#if DEEP_DOG_CAN_AVAILABLE
#ifndef CAN_TX_GPIO
#define CAN_TX_GPIO DEEP_DOG_EXT_PIN_A_GPIO
#endif
#ifndef CAN_RX_GPIO
#define CAN_RX_GPIO DEEP_DOG_EXT_PIN_B_GPIO
#endif
#endif

/** CAN 报文十六进制日志（须在使用方 include 本头或 motor_config 前可被覆盖） */
#ifndef DEEP_DOG_CAN_RX_HEX_LOG
#define DEEP_DOG_CAN_RX_HEX_LOG 0
#endif
#ifndef DEEP_DOG_CAN_TX_HEX_LOG
#define DEEP_DOG_CAN_TX_HEX_LOG 0
#endif

/** TWAI 队列深度（增大可降低高频下发 send timeout） */
#ifndef DEEP_DOG_CAN_TX_QUEUE_SIZE
#define DEEP_DOG_CAN_TX_QUEUE_SIZE 96
#endif
#ifndef DEEP_DOG_CAN_RX_QUEUE_SIZE
#define DEEP_DOG_CAN_RX_QUEUE_SIZE 96
#endif

#endif  // _DEEP_DOG_CAN_CONFIG_H_
