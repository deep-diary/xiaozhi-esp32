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
class VisionFrameHub;

/**
 * 摄像头采集策略（与 VisionFrameHub 的 VisionPublishMode 对应；兼容旧 API）。
 */
enum class DeepDogCaptureMode : uint8_t {
    /** 不发布视频；人脸仍可由 VisionFrameHub 静默采帧 */
    Off = 0,
    /** 兼容旧「定时」：等同 Off（无人脸外的额外行为） */
    PeriodicSample = 1,
    /** 局域网 HTTP MJPEG（/stream） */
    Streaming = 2,
    /** 设备作 RTSP 客户端推 MediaMTX（与 Streaming 互斥） */
    RtspPush = 3,
};

/**
 * DeepDog 板：局域网 HTTP 控制页 +（可选）MJPEG。
 * 采帧/人脸/推流由 VisionFrameHub 统一调度；本类负责 httpd、狗控、MJPEG 发送。
 */
class DeepDogHttpServer {
public:
    DeepDogHttpServer(EspVideo* camera, DogControl* dog, uint16_t port = DEEP_DOG_HTTP_SERVER_PORT);
    ~DeepDogHttpServer();

    void SetVisionHub(VisionFrameHub* hub) { vision_hub_ = hub; }
    VisionFrameHub* vision_hub() const { return vision_hub_; }

    bool Start();
    void Stop();
    bool IsRunning() const { return server_ != nullptr; }

    DeepDogCaptureMode GetCaptureMode() const;
    void SetCaptureMode(DeepDogCaptureMode m);

    int StreamClientCount() const { return stream_clients_.load(std::memory_order_relaxed); }
    void IncStreamClient() { stream_clients_.fetch_add(1, std::memory_order_relaxed); }
    void DecStreamClient() { stream_clients_.fetch_sub(1, std::memory_order_relaxed); }
    bool HasJpegFrame() const;

    uint16_t Port() const { return port_; }

    /** httpd 与 VisionFrameHub 回调使用 */
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
    static void DogCmdTaskEntry(void* arg);
    void DogCmdTaskLoop();

    static void MjpegStreamWorkerEntry(void* arg);
    void MjpegStreamWorkerLoop();

    static void IpGotHandler(void* arg, esp_event_base_t base, int32_t event_id, void* event_data);
    void LogHttpAccessUrls();

    EspVideo* camera_;
    DogControl* dog_;
    VisionFrameHub* vision_hub_ = nullptr;
    uint16_t port_;

    httpd_handle_t server_ = nullptr;
    TaskHandle_t dog_cmd_task_ = nullptr;
    TaskHandle_t mjpeg_stream_tasks_[2] = {nullptr, nullptr};

    std::atomic<bool> worker_stop_{false};
    std::atomic<uint8_t> capture_mode_{static_cast<uint8_t>(DeepDogCaptureMode::Off)};
    std::atomic<int> stream_clients_{0};

    mutable std::mutex jpeg_mutex_;
    std::vector<uint8_t> jpeg_latest_;

    QueueHandle_t dog_cmd_queue_ = nullptr;
    QueueHandle_t mjpeg_stream_queue_ = nullptr;

    bool ip_event_registered_ = false;
};
