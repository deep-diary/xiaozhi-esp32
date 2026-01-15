#ifndef MJPEG_SERVER_H
#define MJPEG_SERVER_H

#include <esp_http_server.h>
#include <string>

/**
 * @brief HTTP MJPEG 服务器
 * 
 * 在ESP32上启动HTTP服务器，提供MJPEG视频流
 * 可通过浏览器直接访问: http://<ESP32_IP>:8080/stream.mjpeg
 */
class MjpegServer {
public:
    /**
     * @brief 构造函数
     * @param port 服务器端口，默认8080
     */
    MjpegServer(uint16_t port = 8080);
    
    ~MjpegServer();

    /**
     * @brief 启动服务器
     * @return true 成功，false 失败
     */
    bool Start();

    /**
     * @brief 停止服务器
     */
    void Stop();

    /**
     * @brief 检查是否正在运行
     * @return true 正在运行，false 未运行
     */
    bool IsRunning() const { return server_ != nullptr; }

    /**
     * @brief 获取访问URL
     * @return URL字符串
     */
    std::string GetUrl() const;

    /**
     * @brief 设置帧率
     * @param fps 帧率（帧/秒）
     */
    void SetFrameRate(int fps) { frame_rate_ = fps; }

    /**
     * @brief 设置JPEG质量
     * @param quality JPEG质量（0-100）
     */
    void SetJpegQuality(int quality) { jpeg_quality_ = quality; }

private:
    httpd_handle_t server_;
    uint16_t port_;
    int frame_rate_;
    int jpeg_quality_;
    
    /**
     * @brief 流处理器
     */
    static esp_err_t StreamHandler(httpd_req_t *req);
};

#endif // MJPEG_SERVER_H

