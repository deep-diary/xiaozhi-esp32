#include "mjpeg_server.h"
#include <esp_log.h>
#include <esp_camera.h>
#include <img_converters.h>
#include <wifi_station.h>
#include <string.h>

#define TAG "MjpegServer"

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

MjpegServer::MjpegServer(uint16_t port)
    : server_(nullptr), port_(port), frame_rate_(10), jpeg_quality_(80) {
}

MjpegServer::~MjpegServer() {
    Stop();
}

std::string MjpegServer::GetUrl() const {
    std::string ip = WifiStation::GetInstance().GetIpAddress();
    return "http://" + ip + ":" + std::to_string(port_) + "/stream.mjpeg";
}

esp_err_t MjpegServer::StreamHandler(httpd_req_t *req) {
    ESP_LOGI(TAG, "客户端连接: %s", req->uri);
    
    // 获取MjpegServer实例
    MjpegServer* server = (MjpegServer*)req->user_ctx;
    
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t * _jpg_buf = NULL;
    char * part_buf[64];

    // 设置响应头
    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "设置响应类型失败");
        return res;
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "获取相机帧失败");
            res = ESP_FAIL;
            break;
        }

        if (fb->format != PIXFORMAT_JPEG) {
            // 如果不是JPEG格式，需要转换
            bool jpeg_converted = frame2jpg(fb, server->jpeg_quality_, &_jpg_buf, &_jpg_buf_len);
            esp_camera_fb_return(fb);
            fb = NULL;
            if (!jpeg_converted) {
                ESP_LOGE(TAG, "JPEG转换失败");
                res = ESP_FAIL;
                break;
            }
        } else {
            _jpg_buf_len = fb->len;
            _jpg_buf = fb->buf;
        }

        // 发送boundary
        if (httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY)) != ESP_OK) {
            ESP_LOGW(TAG, "客户端断开连接");
            res = ESP_FAIL;
            break;
        }

        // 发送Content-Type和Content-Length
        size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
        if (httpd_resp_send_chunk(req, (const char *)part_buf, hlen) != ESP_OK) {
            ESP_LOGW(TAG, "客户端断开连接");
            res = ESP_FAIL;
            break;
        }

        // 发送JPEG数据
        if (httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len) != ESP_OK) {
            ESP_LOGW(TAG, "客户端断开连接");
            res = ESP_FAIL;
            break;
        }

        // 清理
        if (fb) {
            esp_camera_fb_return(fb);
            fb = NULL;
            _jpg_buf = NULL;
        } else if (_jpg_buf) {
            free(_jpg_buf);
            _jpg_buf = NULL;
        }

        // 按帧率延迟
        vTaskDelay(pdMS_TO_TICKS(1000 / server->frame_rate_));
    }

    // 最后清理
    if (fb) {
        esp_camera_fb_return(fb);
    }
    if (_jpg_buf) {
        free(_jpg_buf);
    }

    ESP_LOGI(TAG, "客户端断开");
    return res;
}

bool MjpegServer::Start() {
    if (server_ != nullptr) {
        ESP_LOGW(TAG, "服务器已在运行");
        return true;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port_;
    config.ctrl_port = port_ + 1;
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "正在启动HTTP服务器，端口: %d", port_);
    
    if (httpd_start(&server_, &config) == ESP_OK) {
        // 注册流处理器
        httpd_uri_t stream_uri = {
            .uri       = "/stream.mjpeg",
            .method    = HTTP_GET,
            .handler   = StreamHandler,
            .user_ctx  = this
        };
        
        if (httpd_register_uri_handler(server_, &stream_uri) == ESP_OK) {
            ESP_LOGI(TAG, "MJPEG服务器启动成功");
            ESP_LOGI(TAG, "访问地址: %s", GetUrl().c_str());
            ESP_LOGI(TAG, "帧率: %d fps, JPEG质量: %d", frame_rate_, jpeg_quality_);
            return true;
        } else {
            ESP_LOGE(TAG, "注册URI处理器失败");
            httpd_stop(server_);
            server_ = nullptr;
            return false;
        }
    }

    ESP_LOGE(TAG, "启动HTTP服务器失败");
    return false;
}

void MjpegServer::Stop() {
    if (server_) {
        httpd_stop(server_);
        server_ = nullptr;
        ESP_LOGI(TAG, "MJPEG服务器已停止");
    }
}

