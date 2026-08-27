#pragma once

#include "ws_mcp_config.h"
#include <string>

#if DEEP_DOG_WS_MCP_ENABLE

#include <esp_http_server.h>

#include <cstddef>
#include <map>
#include <mutex>
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

    size_t GetClientCount() const;
    void BroadcastMessage(const std::string& message);

private:
    static esp_err_t WsHandler(httpd_req_t* req);
    static void OnDisconnected(void* arg, esp_event_base_t base, int32_t id, void* data);

    void HandleMessage(httpd_req_t* req, const char* data, size_t len);
    void AddClient(httpd_req_t* req);
    void RemoveClient(httpd_req_t* req);
    void RemoveClientByFd(int sock_fd);

    httpd_handle_t server_handle_ = nullptr;
    uint16_t port_ = DEEP_DOG_WS_MCP_PORT;
    std::map<int, httpd_req_t*> clients_;
    mutable std::mutex clients_mu_;

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
    size_t GetClientCount() const { return 0; }
    void BroadcastMessage(const std::string&) {}
};

#endif  // DEEP_DOG_WS_MCP_ENABLE
