/**
 ****************************************************************************************************
 * @file        tcp_client.h
 * @author      DeepDiary Team
 * @version     V1.0
 * @date        2025-10-23
 * @brief       TCP客户端 - 基于lwip实现摄像头数据流传输
 * @license     Based on ALIENTEK example code
 ****************************************************************************************************
 * @attention
 * 
 * 功能说明：
 * - TCP客户端连接到远程服务器
 * - 实时传输摄像头JPEG帧数据
 * - 支持断线重连
 * - 异步发送和接收
 ****************************************************************************************************
 */

#ifndef __TCP_CLIENT_H__
#define __TCP_CLIENT_H__

#include <string.h>
#include <sys/socket.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

/* TCP客户端配置 */
#define TCP_CLIENT_DEFAULT_IP       "35.192.64.247"    /* 默认远程服务器IP地址 */
#define TCP_CLIENT_DEFAULT_PORT     8080               /* 默认远程服务器端口号 */
#define TCP_CLIENT_RX_BUFSIZE       128                /* 最大接收数据长度 */
#define TCP_CLIENT_SEND_PRIO        10                 /* 发送线程优先级 */
#define TCP_CLIENT_RECV_PRIO        10                 /* 接收线程优先级 */
#define TCP_CLIENT_STACK_SIZE       4096               /* 线程堆栈大小 */

/* TCP客户端状态 */
typedef enum {
    TCP_CLIENT_DISCONNECTED = 0,   /* 未连接 */
    TCP_CLIENT_CONNECTING,         /* 连接中 */
    TCP_CLIENT_CONNECTED,          /* 已连接 */
    TCP_CLIENT_ERROR               /* 错误状态 */
} tcp_client_state_t;

/* TCP客户端配置结构体 */
typedef struct {
    char server_ip[16];            /* 服务器IP地址 */
    uint16_t server_port;          /* 服务器端口号 */
    bool auto_reconnect;           /* 是否自动重连 */
    uint32_t reconnect_interval;   /* 重连间隔(ms) */
} tcp_client_config_t;

/**
 * @brief       初始化TCP客户端
 * @param       config: 客户端配置参数
 * @retval      ESP_OK: 成功, ESP_FAIL: 失败
 */
esp_err_t tcp_client_init(const tcp_client_config_t *config);

/**
 * @brief       启动TCP客户端
 * @param       无
 * @retval      ESP_OK: 成功, ESP_FAIL: 失败
 */
esp_err_t tcp_client_start(void);

/**
 * @brief       停止TCP客户端
 * @param       无
 * @retval      ESP_OK: 成功, ESP_FAIL: 失败
 */
esp_err_t tcp_client_stop(void);

/**
 * @brief       获取TCP客户端连接状态
 * @param       无
 * @retval      tcp_client_state_t: 当前连接状态
 */
tcp_client_state_t tcp_client_get_state(void);

/**
 * @brief       设置服务器地址
 * @param       ip: 服务器IP地址
 * @param       port: 服务器端口号
 * @retval      ESP_OK: 成功, ESP_FAIL: 失败
 */
esp_err_t tcp_client_set_server(const char *ip, uint16_t port);

#ifdef __cplusplus
}
#endif

#endif /* __TCP_CLIENT_H__ */

