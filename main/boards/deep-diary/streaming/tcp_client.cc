/**
 ****************************************************************************************************
 * @file        tcp_client.cc
 * @author      DeepDiary Team
 * @version     V1.0
 * @date        2025-10-23
 * @brief       TCP客户端实现 - 基于lwip实现摄像头数据流传输
 * @license     Based on ALIENTEK example code
 ****************************************************************************************************
 */

#include "tcp_client.h"
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static const char *TAG = "TCP_CLIENT";

/* TCP客户端全局变量 */
static tcp_client_config_t g_tcp_config = {
    .server_ip = TCP_CLIENT_DEFAULT_IP,
    .server_port = TCP_CLIENT_DEFAULT_PORT,
    .auto_reconnect = true,
    .reconnect_interval = 3000
};

static int g_sock = -1;                                    /* socket句柄 */
static tcp_client_state_t g_client_state = TCP_CLIENT_DISCONNECTED;
static TaskHandle_t g_send_task_handle = NULL;
static TaskHandle_t g_recv_task_handle = NULL;
static bool g_tcp_client_running = false;
static char g_recv_buffer[TCP_CLIENT_RX_BUFSIZE];          /* 接收缓冲区 */

/**
 * @brief       TCP发送线程
 * @param       pvParameters: 线程参数
 * @retval      无
 */
static void tcp_client_send_task(void *pvParameters)
{
    camera_fb_t *camera_frame = NULL;
    int send_result = 0;
    size_t total_sent = 0;
    size_t bytes_to_send = 0;
    uint8_t *data_ptr = NULL;
    static uint32_t frame_counter = 0;
    
    ESP_LOGI(TAG, "TCP发送线程已启动");
    
    while (g_tcp_client_running)
    {
        /* 只有在连接状态才发送数据 */
        if (g_client_state == TCP_CLIENT_CONNECTED && g_sock >= 0)
        {
            /* 获取摄像头帧 */
            camera_frame = esp_camera_fb_get();
            if (camera_frame != NULL)
            {
                frame_counter++;
                
                /* 第一帧时显示格式信息 */
                if (frame_counter == 1)
                {
                    ESP_LOGI(TAG, "摄像头格式: %d, 分辨率: %dx%d, 大小: %d 字节",
                            camera_frame->format, 
                            camera_frame->width, 
                            camera_frame->height,
                            camera_frame->len);
                    
                    /* 检查是否为 JPEG 格式 */
                    if (camera_frame->format == PIXFORMAT_JPEG)
                    {
                        ESP_LOGI(TAG, "✓ JPEG 格式正确");
                        /* 验证 JPEG 头尾标记 */
                        if (camera_frame->len >= 2)
                        {
                            if (camera_frame->buf[0] == 0xFF && camera_frame->buf[1] == 0xD8)
                            {
                                ESP_LOGI(TAG, "✓ JPEG 起始标记正确 (0xFFD8)");
                            }
                            if (camera_frame->buf[camera_frame->len-2] == 0xFF && 
                                camera_frame->buf[camera_frame->len-1] == 0xD9)
                            {
                                ESP_LOGI(TAG, "✓ JPEG 结束标记正确 (0xFFD9)");
                            }
                        }
                    }
                    else
                    {
                        ESP_LOGE(TAG, "✗ 错误！摄像头格式不是 JPEG，是: %d", camera_frame->format);
                    }
                }
                
                /* 循环发送，确保发送完整 */
                total_sent = 0;
                bytes_to_send = camera_frame->len;
                data_ptr = camera_frame->buf;
                
                while (total_sent < bytes_to_send)
                {
                    send_result = send(g_sock, data_ptr + total_sent, bytes_to_send - total_sent, 0);
                    
                    if (send_result < 0)
                    {
                        ESP_LOGE(TAG, "发送失败: errno %d", errno);
                        g_client_state = TCP_CLIENT_ERROR;
                        break;
                    }
                    else if (send_result == 0)
                    {
                        ESP_LOGW(TAG, "发送返回0，连接可能断开");
                        g_client_state = TCP_CLIENT_ERROR;
                        break;
                    }
                    
                    total_sent += send_result;
                }
                
                /* 每100帧显示一次统计 */
                if (frame_counter % 100 == 0)
                {
                    ESP_LOGI(TAG, "已发送 %lu 帧，最后一帧: %d 字节", frame_counter, camera_frame->len);
                }
                
                /* 释放帧缓冲 */
                esp_camera_fb_return(camera_frame);
            }
            else
            {
                ESP_LOGW(TAG, "获取摄像头帧失败");
            }
        }
        
        /* 延时控制发送频率 */
        vTaskDelay(pdMS_TO_TICKS(33)); /* 约30fps */
    }
    
    ESP_LOGI(TAG, "TCP发送线程已退出，共发送 %lu 帧", frame_counter);
    g_send_task_handle = NULL;
    vTaskDelete(NULL);
}

/**
 * @brief       TCP接收线程
 * @param       pvParameters: 线程参数
 * @retval      无
 */
static void tcp_client_recv_task(void *pvParameters)
{
    struct sockaddr_in server_addr;
    int recv_len = 0;
    int connect_result = 0;
    
    ESP_LOGI(TAG, "TCP接收线程已启动");
    
    while (g_tcp_client_running)
    {
        /* 创建socket */
        g_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (g_sock < 0)
        {
            ESP_LOGE(TAG, "创建socket失败: errno %d", errno);
            g_client_state = TCP_CLIENT_ERROR;
            vTaskDelay(pdMS_TO_TICKS(g_tcp_config.reconnect_interval));
            continue;
        }
        
        ESP_LOGI(TAG, "Socket创建成功，准备连接到 %s:%d", 
                 g_tcp_config.server_ip, g_tcp_config.server_port);
        
        /* 配置服务器地址 */
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(g_tcp_config.server_port);
        inet_pton(AF_INET, g_tcp_config.server_ip, &server_addr.sin_addr);
        
        /* 连接到服务器 */
        g_client_state = TCP_CLIENT_CONNECTING;
        connect_result = connect(g_sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
        
        if (connect_result != 0)
        {
            ESP_LOGE(TAG, "连接失败: errno %d", errno);
            g_client_state = TCP_CLIENT_DISCONNECTED;
            close(g_sock);
            g_sock = -1;
            
            /* 如果启用自动重连，等待后重试 */
            if (g_tcp_config.auto_reconnect)
            {
                ESP_LOGI(TAG, "等待 %d ms 后重连...", g_tcp_config.reconnect_interval);
                vTaskDelay(pdMS_TO_TICKS(g_tcp_config.reconnect_interval));
                continue;
            }
            else
            {
                break;
            }
        }
        
        ESP_LOGI(TAG, "TCP连接成功！");
        g_client_state = TCP_CLIENT_CONNECTED;
        
        /* 接收循环 */
        while (g_tcp_client_running && g_client_state == TCP_CLIENT_CONNECTED)
        {
            recv_len = recv(g_sock, g_recv_buffer, sizeof(g_recv_buffer) - 1, 0);
            
            if (recv_len < 0)
            {
                ESP_LOGE(TAG, "接收失败: errno %d", errno);
                g_client_state = TCP_CLIENT_ERROR;
                break;
            }
            else if (recv_len == 0)
            {
                ESP_LOGW(TAG, "服务器断开连接");
                g_client_state = TCP_CLIENT_DISCONNECTED;
                break;
            }
            else
            {
                /* 处理接收到的数据 */
                g_recv_buffer[recv_len] = '\0';
                ESP_LOGI(TAG, "接收 %d 字节: %s", recv_len, g_recv_buffer);
            }
        }
        
        /* 关闭连接 */
        if (g_sock >= 0)
        {
            close(g_sock);
            g_sock = -1;
        }
        
        g_client_state = TCP_CLIENT_DISCONNECTED;
        ESP_LOGI(TAG, "TCP连接已断开");
        
        /* 如果不自动重连或已停止运行，退出循环 */
        if (!g_tcp_config.auto_reconnect || !g_tcp_client_running)
        {
            break;
        }
        
        /* 等待后重连 */
        ESP_LOGI(TAG, "等待 %d ms 后重连...", g_tcp_config.reconnect_interval);
        vTaskDelay(pdMS_TO_TICKS(g_tcp_config.reconnect_interval));
    }
    
    ESP_LOGI(TAG, "TCP接收线程已退出");
    g_recv_task_handle = NULL;
    vTaskDelete(NULL);
}

/**
 * @brief       初始化TCP客户端
 * @param       config: 客户端配置参数
 * @retval      ESP_OK: 成功, ESP_FAIL: 失败
 */
esp_err_t tcp_client_init(const tcp_client_config_t *config)
{
    if (config != NULL)
    {
        /* 复制配置 */
        strncpy(g_tcp_config.server_ip, config->server_ip, sizeof(g_tcp_config.server_ip) - 1);
        g_tcp_config.server_port = config->server_port;
        g_tcp_config.auto_reconnect = config->auto_reconnect;
        g_tcp_config.reconnect_interval = config->reconnect_interval;
    }
    
    ESP_LOGI(TAG, "TCP客户端初始化完成");
    ESP_LOGI(TAG, "服务器: %s:%d", g_tcp_config.server_ip, g_tcp_config.server_port);
    ESP_LOGI(TAG, "自动重连: %s", g_tcp_config.auto_reconnect ? "是" : "否");
    
    return ESP_OK;
}

/**
 * @brief       启动TCP客户端
 * @param       无
 * @retval      ESP_OK: 成功, ESP_FAIL: 失败
 */
esp_err_t tcp_client_start(void)
{
    if (g_tcp_client_running)
    {
        ESP_LOGW(TAG, "TCP客户端已经在运行");
        return ESP_OK;
    }
    
    g_tcp_client_running = true;
    g_client_state = TCP_CLIENT_DISCONNECTED;
    
    /* 创建接收任务 */
    BaseType_t ret = xTaskCreate(tcp_client_recv_task, 
                                  "tcp_recv", 
                                  TCP_CLIENT_STACK_SIZE, 
                                  NULL, 
                                  TCP_CLIENT_RECV_PRIO, 
                                  &g_recv_task_handle);
    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "创建接收任务失败");
        g_tcp_client_running = false;
        return ESP_FAIL;
    }
    
    /* 创建发送任务 */
    ret = xTaskCreate(tcp_client_send_task, 
                      "tcp_send", 
                      TCP_CLIENT_STACK_SIZE, 
                      NULL, 
                      TCP_CLIENT_SEND_PRIO, 
                      &g_send_task_handle);
    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "创建发送任务失败");
        g_tcp_client_running = false;
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "TCP客户端已启动");
    return ESP_OK;
}

/**
 * @brief       停止TCP客户端
 * @param       无
 * @retval      ESP_OK: 成功, ESP_FAIL: 失败
 */
esp_err_t tcp_client_stop(void)
{
    if (!g_tcp_client_running)
    {
        ESP_LOGW(TAG, "TCP客户端未运行");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "正在停止TCP客户端...");
    
    /* 设置停止标志 */
    g_tcp_client_running = false;
    
    /* 关闭socket */
    if (g_sock >= 0)
    {
        close(g_sock);
        g_sock = -1;
    }
    
    /* 等待任务退出 */
    int timeout = 50; /* 5秒超时 */
    while ((g_recv_task_handle != NULL || g_send_task_handle != NULL) && timeout > 0)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
        timeout--;
    }
    
    g_client_state = TCP_CLIENT_DISCONNECTED;
    ESP_LOGI(TAG, "TCP客户端已停止");
    
    return ESP_OK;
}

/**
 * @brief       获取TCP客户端连接状态
 * @param       无
 * @retval      tcp_client_state_t: 当前连接状态
 */
tcp_client_state_t tcp_client_get_state(void)
{
    return g_client_state;
}

/**
 * @brief       设置服务器地址
 * @param       ip: 服务器IP地址
 * @param       port: 服务器端口号
 * @retval      ESP_OK: 成功, ESP_FAIL: 失败
 */
esp_err_t tcp_client_set_server(const char *ip, uint16_t port)
{
    if (ip == NULL)
    {
        return ESP_FAIL;
    }
    
    /* 如果正在运行，先停止 */
    bool was_running = g_tcp_client_running;
    if (was_running)
    {
        tcp_client_stop();
    }
    
    /* 更新配置 */
    strncpy(g_tcp_config.server_ip, ip, sizeof(g_tcp_config.server_ip) - 1);
    g_tcp_config.server_port = port;
    
    ESP_LOGI(TAG, "服务器地址已更新: %s:%d", g_tcp_config.server_ip, g_tcp_config.server_port);
    
    /* 如果之前在运行，重新启动 */
    if (was_running)
    {
        vTaskDelay(pdMS_TO_TICKS(500)); /* 延时等待完全停止 */
        tcp_client_start();
    }
    
    return ESP_OK;
}

