#pragma once

#include "ws_mcp_config.h"
#include <string>

#if DEEP_DOG_WS_MCP_ENABLE

#include <esp_http_server.h>

#include <cstddef>
#include <map>
#include <string>

/**
 * 局域网 WebSocket → MCP 通用桥接（与 http-server 独立 httpd）。
 * 收 JSON-RPC / MCP payload → McpServer::ParseMessage；响应经 BroadcastMessage 回推。
 */
class DeepDogWsMcpServer {
public:
    DeepDogWsMcpServer();
    ~DeepDogWsMcpServer();

    bool Start(uint16_t port = DEEP_DOG_WS_MCP_PORT);
    void Stop();

    bool IsRunning() const { return server_handle_ != nullptr; }
    uint16_t Port() const { return port_; }
    const char* Path() const { return DEEP_DOG_WS_MCP_PATH; }

    /** 底层 httpd 句柄（供板层挂载额外 REST 路由，如 /api/datapoints/... ，省内存） */
    void* HttpdHandle() const { return server_handle_; }

    size_t GetClientCount() const;
    void BroadcastMessage(const std::string& message);

    /**
     * 摘除一个失联/超时的 WS 客户端（广播发送失败时调用）。
     *
     * 背景（F-14）：浏览器/外部脚本常保持监听；对端不再读取时
     * `httpd_ws_send_frame_async` 会以 `ESP_ERR_TIMEOUT(258)` 反复失败，
     * 既刷日志也拖慢 httpd 任务。此处失败即摘除，避免反复重试。
     */
    void DropClient(int fd);

private:
    static esp_err_t WsHandler(httpd_req_t* req);

    void HandleMessage(httpd_req_t* req, const char* data, size_t len);
    void AddClient(httpd_req_t* req);
    void RemoveClient(httpd_req_t* req);

    httpd_handle_t server_handle_ = nullptr;
    uint16_t port_ = DEEP_DOG_WS_MCP_PORT;
    std::map<int, httpd_req_t*> clients_;

    static DeepDogWsMcpServer* instance_;
};

#else

class DeepDogWsMcpServer {
public:
    bool Start(uint16_t = DEEP_DOG_WS_MCP_PORT) { return false; }
    void Stop() {}
    bool IsRunning() const { return false; }
    uint16_t Port() const { return DEEP_DOG_WS_MCP_PORT; }
    const char* Path() const { return DEEP_DOG_WS_MCP_PATH; }
    void* HttpdHandle() const { return nullptr; }
    size_t GetClientCount() const { return 0; }
    void BroadcastMessage(const std::string&) {}
    void DropClient(int) {}
};

#endif  // DEEP_DOG_WS_MCP_ENABLE
