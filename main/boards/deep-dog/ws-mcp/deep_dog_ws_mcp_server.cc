#include "ws-mcp/deep_dog_ws_mcp_server.h"

#if DEEP_DOG_WS_MCP_ENABLE

#include "mcp_server.h"

#include <cJSON.h>
#include <esp_heap_caps.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>
#include <cstdlib>

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

    httpd_uri_t ws_uri = {
        .uri = DEEP_DOG_WS_MCP_PATH,
        .method = HTTP_GET,
        .handler = WsHandler,
        .user_ctx = nullptr,
        .is_websocket = true,
    };

    esp_err_t start_err = ESP_FAIL;
    for (int attempt = 1; attempt <= 3; ++attempt) {
        const size_t free_int = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        const size_t largest_int = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        start_err = httpd_start(&server_handle_, &config);
        if (start_err == ESP_OK) {
            break;
        }
        ESP_LOGW(TAG, "httpd_start attempt %d/3 failed port=%u err=%s free_int=%u largest_int=%u",
                 attempt, static_cast<unsigned>(port_), esp_err_to_name(start_err), (unsigned)free_int,
                 (unsigned)largest_int);
        server_handle_ = nullptr;
        vTaskDelay(pdMS_TO_TICKS(500));
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

    ESP_LOGI(TAG, "WS MCP bridge ws://<ip>:%u%s (generic McpServer::ParseMessage)",
             static_cast<unsigned>(port_), DEEP_DOG_WS_MCP_PATH);
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
        ESP_LOGE(TAG, "message too long: %zu", len);
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
            McpServer::GetInstance().ParseMessage(payload);
            cJSON_Delete(payload);
        }
    } else {
        payload = cJSON_Duplicate(root, 1);
        if (payload) {
            McpServer::GetInstance().ParseMessage(payload);
            cJSON_Delete(payload);
        }
    }

    if (!payload) {
        ESP_LOGE(TAG, "invalid MCP envelope");
    }
    cJSON_Delete(root);
}

void DeepDogWsMcpServer::AddClient(httpd_req_t* req) {
    const int sock_fd = httpd_req_to_sockfd(req);
    if (clients_.find(sock_fd) == clients_.end()) {
        clients_[sock_fd] = req;
        ESP_LOGI(TAG, "client connected fd=%d total=%zu", sock_fd, clients_.size());
    }
}

void DeepDogWsMcpServer::RemoveClient(httpd_req_t* req) {
    const int sock_fd = httpd_req_to_sockfd(req);
    clients_.erase(sock_fd);
    ESP_LOGI(TAG, "client disconnected fd=%d total=%zu", sock_fd, clients_.size());
}

size_t DeepDogWsMcpServer::GetClientCount() const {
    return clients_.size();
}

struct WsBroadcastJob {
    httpd_handle_t server;
    int fd;
    char* payload;
    size_t len;
};

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
    }
    free(job->payload);
    free(job);
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
        job->fd = fd;
        job->len = message.length();
        job->payload = static_cast<char*>(malloc(message.length() + 1));
        if (!job->payload) {
            free(job);
            continue;
        }
        memcpy(job->payload, message.c_str(), message.length());
        job->payload[message.length()] = '\0';

        if (httpd_queue_work(server_handle_, WsBroadcastSendJob, job) != ESP_OK) {
            free(job->payload);
            free(job);
        }
    }
}

#endif  // DEEP_DOG_WS_MCP_ENABLE
