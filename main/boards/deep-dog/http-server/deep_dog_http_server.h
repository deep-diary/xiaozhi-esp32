#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <esp_event.h>
#include <esp_http_server.h>

#include "http_server_config.h"

class EspVideo;
class DogControl;

/**
 * 摄像头采集策略（与连续视频流/人脸管线解耦，由独立 worker 驱动）。
 */
enum class DeepDogCaptureMode : uint8_t {
    /** 不采帧，不占编码资源 */
    Off = 0,
    /** 约 1Hz CaptureOnly，供后续人脸/检测挂接；不刷 LCD */
    PeriodicSample = 1,
    /** 按目标帧率采帧并编码 JPEG，供 MJPEG 拉流读最新一帧 */
    Streaming = 2,
};

/**
 * DeepDog 板：局域网 HTTP 控制页 + MJPEG（multipart/x-mixed-replace）。
 * 运动指令经队列由独立任务调用 DogControl，避免在 httpd 回调里长时间阻塞。
 */
class DeepDogHttpServer {
public:
    DeepDogHttpServer(EspVideo* camera, DogControl* dog, uint16_t port = DEEP_DOG_HTTP_SERVER_PORT);
    ~DeepDogHttpServer();

    bool Start();
    void Stop();
    bool IsRunning() const { return server_ != nullptr; }

    DeepDogCaptureMode GetCaptureMode() const {
        return static_cast<DeepDogCaptureMode>(capture_mode_.load(std::memory_order_acquire));
    }
    void SetCaptureMode(DeepDogCaptureMode m);

    int StreamClientCount() const { return stream_clients_.load(std::memory_order_relaxed); }
    void IncStreamClient() { stream_clients_.fetch_add(1, std::memory_order_relaxed); }
    void DecStreamClient() { stream_clients_.fetch_sub(1, std::memory_order_relaxed); }
    bool HasJpegFrame() const;

    uint16_t Port() const { return port_; }

    /** httpd 与 CameraWorker 回调使用 */
    void PublishJpeg(std::vector<uint8_t>&& jpeg);
    bool CopyLatestJpeg(std::vector<uint8_t>* out) const;

    /** httpd 回调投递狗指令（非阻塞） */
    bool TryEnqueueDogCmd(uint8_t cmd);

    /**
     * 将已通过 httpd_req_async_handler_begin 得到的 /stream 请求交给 MJPEG 专用任务，
     * 避免长循环占用 httpd 线程导致 /api/cmd 等无法响应。
     */
    bool EnqueueMjpegStreamJob(httpd_req_t* async_req);

    EspVideo* camera() const { return camera_; }
    DogControl* dog() const { return dog_; }

    void RequestStopWorker() { worker_stop_.store(true, std::memory_order_release); }
    bool WorkerStopRequested() const { return worker_stop_.load(std::memory_order_acquire); }

private:
    static void CameraWorkerEntry(void* arg);
    void CameraWorkerLoop();

    static void DogCmdTaskEntry(void* arg);
    void DogCmdTaskLoop();

    static void MjpegStreamWorkerEntry(void* arg);
    void MjpegStreamWorkerLoop();

    bool EncodeCurrentFrameToJpeg(std::vector<uint8_t>* out);
    /** submit_face_for_ai：Streaming 路径在 JPEG 编码前把 RGB565 帧送入人脸任务（方案 B，不重画 MJPEG）。 */
    bool EncodePackedJpegFromCamera(std::vector<uint8_t>* out, bool submit_face_for_ai);

    static void IpGotHandler(void* arg, esp_event_base_t base, int32_t event_id, void* event_data);
    void LogHttpAccessUrls();

    EspVideo* camera_;
    DogControl* dog_;
    uint16_t port_;

    httpd_handle_t server_ = nullptr;
    TaskHandle_t camera_worker_ = nullptr;
    TaskHandle_t dog_cmd_task_ = nullptr;
    TaskHandle_t mjpeg_stream_tasks_[2] = {nullptr, nullptr};

    std::atomic<bool> worker_stop_{false};
    std::atomic<uint8_t> capture_mode_{static_cast<uint8_t>(DeepDogCaptureMode::Off)};
    std::atomic<int> stream_clients_{0};

    int stream_target_fps_ = 4;  // 联调：与人脸检测解耦，略降以免 JPEG/httpd 抢 CPU
    int jpeg_quality_ = 55;

    mutable std::mutex jpeg_mutex_;
    std::vector<uint8_t> jpeg_latest_;

    QueueHandle_t dog_cmd_queue_ = nullptr;
    QueueHandle_t mjpeg_stream_queue_ = nullptr;

    bool ip_event_registered_ = false;
};
