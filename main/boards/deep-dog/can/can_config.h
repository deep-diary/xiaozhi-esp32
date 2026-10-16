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

/**
 * CAN 心跳帧（联调自检）：每 DEEP_DOG_CAN_HEARTBEAT_MS 毫秒向 MQTT 透传管线注入一条
 * **模拟 RX 帧**（不写总线），用于区分「透传链路故障」与「总线无报文」。0 = 关闭。
 */
#ifndef DEEP_DOG_CAN_HEARTBEAT_MS
#define DEEP_DOG_CAN_HEARTBEAT_MS 2000
#endif

/** 心跳帧 ID：29 位扩展，cmd_type=0x1F 保留段且 master_id=0，不与电机协议（master_id=0xFD）冲突 */
#ifndef DEEP_DOG_CAN_HEARTBEAT_ID
#define DEEP_DOG_CAN_HEARTBEAT_ID 0x1F000000UL
#endif

/**
 * CAN 1 Mbps 采样点：**默认 0** = 用 IDF `TWAI_TIMING_CONFIG_1MBITS()`，即
 * **75% 采样点 / Tq=125ns / 8Tq**（tseg_1=5, tseg_2=2）—— 实测与 PCAN 配置一致，故保持默认。
 * 1 = 换成 87.5%（8Tq：tseg_1=6, tseg_2=1），仅当对端确为 87.5% 时才需要。
 * 采样点不一致时，典型症状是「能收到、发不出去」（发送读 ACK 槽出错 → TEC 顶满 → Bus-Off）。
 */
#ifndef DEEP_DOG_CAN_TIMING_875
#define DEEP_DOG_CAN_TIMING_875 0
#endif

/**
 * **LISTEN_ONLY 诊断模式**：1 = 控制器只听（`TWAI_MODE_LISTEN_ONLY`）——
 * **不发 ACK、不发 error flag**，物理上不可能干扰总线。
 * 用于隔离「本节点是否在干扰总线」与「接收链路是否正常」；
 * 该模式下写总线一律失败（`writeFrame` 返回 false），仅作诊断用。
 */
#ifndef DEEP_DOG_CAN_LISTEN_ONLY
#define DEEP_DOG_CAN_LISTEN_ONLY 0
#endif

/**
 * CAN 总线健康检查周期（ms）：轮询 TWAI 状态，Bus-Off / Stopped 时自恢复。
 * TWAI 进入 Bus-Off 后不会自愈，必须 twai_initiate_recovery + twai_start，否则永久收不到帧。
 */
#ifndef DEEP_DOG_CAN_HEALTH_CHECK_MS
#define DEEP_DOG_CAN_HEALTH_CHECK_MS 1000
#endif

/**
 * CAN **周期发送帧**：真正往总线写帧（与 MQTT 心跳 0x1F000000 不同，后者不写总线）。
 * 周期 ms，**默认 1000**（设备正常按 1 帧/s 外发）；0 = 关闭。
 * ⚠️ 若总线上无人 ACK（或总线被某节点重传风暴占满），发送会失败并退避（见板级 CanRxTask），
 *    持续失败时 TEC 会顶满 → Bus-Off。
 */
#ifndef DEEP_DOG_CAN_TX_TEST_MS
#define DEEP_DOG_CAN_TX_TEST_MS 1000
#endif
#ifndef DEEP_DOG_CAN_TX_TEST_ID
#define DEEP_DOG_CAN_TX_TEST_ID 0x1F000002UL
#endif
/** 1 = 29 位扩展帧；0 = 11 位标准帧（对端只认标准帧时用 0） */
#ifndef DEEP_DOG_CAN_TX_TEST_EXT
#define DEEP_DOG_CAN_TX_TEST_EXT 1
#endif

/**
 * CAN 周期健康日志（串口直读，**不经 MQTT**）：state/rx_fps/rx_total/tx_total/rx_err/tx_err/
 * bus_err/bus_off/recover。周期 ms，**默认 5000**；0 = 关闭（联调结束可关）。
 */
#ifndef DEEP_DOG_CAN_HEALTH_LOG_MS
#define DEEP_DOG_CAN_HEALTH_LOG_MS 5000
#endif

/** TWAI 队列深度（增大可降低高频下发 send timeout） */
#ifndef DEEP_DOG_CAN_TX_QUEUE_SIZE
#define DEEP_DOG_CAN_TX_QUEUE_SIZE 96
#endif
#ifndef DEEP_DOG_CAN_RX_QUEUE_SIZE
#define DEEP_DOG_CAN_RX_QUEUE_SIZE 96
#endif

#endif  // _DEEP_DOG_CAN_CONFIG_H_
