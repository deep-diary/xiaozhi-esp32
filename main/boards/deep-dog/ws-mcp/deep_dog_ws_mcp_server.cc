#include "ws-mcp/deep_dog_ws_mcp_server.h"
#include "ws-mcp/datapoint_mcp.h"

#if DEEP_DOG_WS_MCP_ENABLE

#include "mcp_server.h"
#include <wifi_manager.h>

#include <cJSON.h>
#include <esp_heap_caps.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>
#include <cstdlib>
#include <string>

static const char* TAG = "dog_ws_mcp";

DeepDogWsMcpServer* DeepDogWsMcpServer::instance_ = nullptr;

DeepDogWsMcpServer::DeepDogWsMcpServer() {
    instance_ = this;
}

DeepDogWsMcpServer::~DeepDogWsMcpServer() {
    Stop();
    instance_ = nullptr;
}

esp_err_t DeepDogWsMcpServer::WsHandler(httpd_req_t* req) {
    if (instance_ == nullptr) {
        return ESP_FAIL;
    }

    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WebSocket handshake ok");
        instance_->AddClient(req);
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt {};
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "recv frame len failed: %d", ret);
        return ret;
    }

    uint8_t* buf = nullptr;
    if (ws_pkt.len > 0) {
        buf = static_cast<uint8_t*>(calloc(1, ws_pkt.len + 1));
        if (!buf) {
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            free(buf);
            return ret;
        }
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        instance_->RemoveClient(req);
        free(buf);
        return ESP_OK;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT && ws_pkt.len > 0 && buf != nullptr) {
        buf[ws_pkt.len] = '\0';
        instance_->HandleMessage(req, reinterpret_cast<const char*>(buf), ws_pkt.len);
    }

    free(buf);
    return ESP_OK;
}

bool DeepDogWsMcpServer::Start(uint16_t port) {
    if (server_handle_) {
        return true;
    }
    port_ = port;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port_;
    config.max_open_sockets = 7;
    config.ctrl_port = 32770;
    // 允许通配符 URI（如 /api/datapoints/module/xxx）；默认 NULL 仅精确匹配
    config.uri_match_fn = httpd_uri_match_wildcard;

    httpd_uri_t ws_uri = {
        .uri = DEEP_DOG_WS_MCP_PATH,
        .method = HTTP_GET,
        .handler = WsHandler,
        .user_ctx = nullptr,
        .is_websocket = true,
    };

    esp_err_t start_err = ESP_FAIL;
    // F-13：httpd 任务需要一块**连续 internal**（默认栈 4096B）。实测开机阶段 `largest_int`
    // 可能只有 ~3.5KB（OTA/TLS 缓冲区尚未释放）→ `httpd_start` 失败，且旧实现 3 次×0.5s 用完后
    // **本会话再没有 WS-MCP / 数据点 REST**（TC1302–TC1315、TC1252–TC1260 全不可用）。
    // 修复：把重试窗口拉长到 ~4s（内存约在 15~16s 才回稳），仍保持**轻量**（不长时间阻塞启动路径）。
    constexpr int kAttempts = 8;
    constexpr int kRetryGapMs = 500;
    for (int attempt = 1; attempt <= kAttempts; ++attempt) {
        const size_t free_int = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        const size_t largest_int = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        start_err = httpd_start(&server_handle_, &config);
        if (start_err == ESP_OK) {
            if (attempt > 1) {
                ESP_LOGI(TAG, "httpd_start ok on attempt %d/%d (free_int=%u largest_int=%u)",
                         attempt, kAttempts, (unsigned)free_int, (unsigned)largest_int);
            }
            break;
        }
        ESP_LOGW(TAG, "httpd_start attempt %d/%d failed port=%u err=%s free_int=%u largest_int=%u",
                 attempt, kAttempts, static_cast<unsigned>(port_), esp_err_to_name(start_err),
                 (unsigned)free_int, (unsigned)largest_int);
        server_handle_ = nullptr;
        vTaskDelay(pdMS_TO_TICKS(kRetryGapMs));
    }
    if (start_err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed on port %u err=%s", static_cast<unsigned>(port_), esp_err_to_name(start_err));
        server_handle_ = nullptr;
        return false;
    }
    if (httpd_register_uri_handler(server_handle_, &ws_uri) != ESP_OK) {
        ESP_LOGE(TAG, "register %s failed", DEEP_DOG_WS_MCP_PATH);
        httpd_stop(server_handle_);
        server_handle_ = nullptr;
        return false;
    }

    {
        const std::string ip = WifiManager::GetInstance().GetIpAddress();
        ESP_LOGI(TAG, "WS MCP bridge ws://%s:%u%s (generic McpServer::ParseMessage)",
                 ip.empty() ? "<ip>" : ip.c_str(), static_cast<unsigned>(port_),
                 DEEP_DOG_WS_MCP_PATH);
    }
    return true;
}

void DeepDogWsMcpServer::Stop() {
    if (server_handle_) {
        httpd_stop(server_handle_);
        server_handle_ = nullptr;
        clients_.clear();
        ESP_LOGI(TAG, "WS MCP server stopped");
    }
}

void DeepDogWsMcpServer::HandleMessage(httpd_req_t* req, const char* data, size_t len) {
    (void)req;
    if (!data || len == 0) {
        return;
    }
    if (len > DEEP_DOG_WS_MCP_MAX_MSG_LEN) {
        ESP_LOGE(TAG, "message too long: %u", static_cast<unsigned>(len));
        return;
    }

    char* temp_buf = static_cast<char*>(malloc(len + 1));
    if (!temp_buf) {
        return;
    }
    memcpy(temp_buf, data, len);
    temp_buf[len] = '\0';

    cJSON* root = cJSON_Parse(temp_buf);
    free(temp_buf);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse failed");
        return;
    }

    cJSON* payload = nullptr;
    cJSON* type = cJSON_GetObjectItem(root, "type");
    if (type && cJSON_IsString(type) && strcmp(type->valuestring, "mcp") == 0) {
        payload = cJSON_GetObjectItem(root, "payload");
        if (payload) {
            cJSON_DetachItemViaPointer(root, payload);
        }
    } else {
        payload = cJSON_Duplicate(root, 1);
    }

    if (!payload) {
        ESP_LOGE(TAG, "invalid MCP envelope");
        cJSON_Delete(root);
        return;
    }

    // 本地 WS-MCP 是「全量」通道（docs/interface/02-ws-contract.md）：
    // 数据点自动工具为 user_only（零 per-tool 堆开销，已由板级 InitializeTools 挂接 auto source，
    // 见 F-12），故 tools/list 强制带 withUserTools=true 使其在本通道可见。
    cJSON* method = cJSON_GetObjectItem(payload, "method");
    if (method && cJSON_IsString(method) && strcmp(method->valuestring, "tools/list") == 0) {
        cJSON* params = cJSON_GetObjectItem(payload, "params");
        if (!params || !cJSON_IsObject(params)) {
            params = cJSON_AddObjectToObject(payload, "params");
        }
        if (params && !cJSON_GetObjectItem(params, "withUserTools")) {
            cJSON_AddBoolToObject(params, "withUserTools", true);
        }
    }

    McpServer::GetInstance().ParseMessage(payload);
    cJSON_Delete(payload);
    cJSON_Delete(root);
}

void DeepDogWsMcpServer::AddClient(httpd_req_t* req) {
    const int sock_fd = httpd_req_to_sockfd(req);
    if (clients_.find(sock_fd) == clients_.end()) {
        clients_[sock_fd] = req;
        ESP_LOGI(TAG, "client connected fd=%d total=%u", sock_fd,
                 static_cast<unsigned>(clients_.size()));
    }
}

void DeepDogWsMcpServer::RemoveClient(httpd_req_t* req) {
    const int sock_fd = httpd_req_to_sockfd(req);
    clients_.erase(sock_fd);
    ESP_LOGI(TAG, "client disconnected fd=%d total=%u", sock_fd,
             static_cast<unsigned>(clients_.size()));
}

size_t DeepDogWsMcpServer::GetClientCount() const {
    return clients_.size();
}

struct WsBroadcastJob {
    httpd_handle_t server;
    DeepDogWsMcpServer* self;
    int fd;
    char* payload;
    size_t len;
};

/** 广播载荷缓冲：**优先 PSRAM**（internal 紧张时少一次内部堆分配，见 S09 / F-12）。 */
static char* AllocBroadcastPayload(size_t len) {
    char* p = static_cast<char*>(heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!p) {
        p = static_cast<char*>(malloc(len));
    }
    return p;
}

static void WsBroadcastSendJob(void* arg) {
    WsBroadcastJob* job = static_cast<WsBroadcastJob*>(arg);
    httpd_ws_frame_t ws_pkt {};
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    ws_pkt.payload = reinterpret_cast<uint8_t*>(job->payload);
    ws_pkt.len = job->len;
    ws_pkt.final = true;

    const esp_err_t ret = httpd_ws_send_frame_async(job->server, job->fd, &ws_pkt);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "broadcast send failed fd=%d err=%d", job->fd, ret);
        // 对端已不读/已断开：摘除，避免下一次广播继续在同一 fd 上超时（F-14）
        if (job->self) {
            job->self->DropClient(job->fd);
        }
    }
    heap_caps_free(job->payload);
    free(job);
}

void DeepDogWsMcpServer::DropClient(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) {
        return;
    }
    clients_.erase(it);
    ESP_LOGW(TAG, "dropped stale ws client fd=%d (remaining=%u)", fd,
             static_cast<unsigned>(clients_.size()));
}

void DeepDogWsMcpServer::BroadcastMessage(const std::string& message) {
    if (!server_handle_ || clients_.empty()) {
        return;
    }

    for (const auto& [fd, _req] : clients_) {
        (void)_req;
        WsBroadcastJob* job = static_cast<WsBroadcastJob*>(malloc(sizeof(WsBroadcastJob)));
        if (!job) {
            continue;
        }
        job->server = server_handle_;
        job->self = this;
        job->fd = fd;
        job->len = message.length();
        job->payload = AllocBroadcastPayload(message.length() + 1);
        if (!job->payload) {
            free(job);
            continue;
        }
        memcpy(job->payload, message.c_str(), message.length());
        job->payload[message.length()] = '\0';

        if (httpd_queue_work(server_handle_, WsBroadcastSendJob, job) != ESP_OK) {
            heap_caps_free(job->payload);
            free(job);
        }
    }
}

#endif  // DEEP_DOG_WS_MCP_ENABLE
