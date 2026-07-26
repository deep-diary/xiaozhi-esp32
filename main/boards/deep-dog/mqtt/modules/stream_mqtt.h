#pragma once

#include "vision/vision_types.h"

#include <esp_timer.h>

#include <atomic>
#include <string>

class DeepDogMqttClient;
class VisionFrameHub;
class DeepDogHttpServer;

/** stream/cmd 订阅 + stream/status retain；stream/photo 拍照视觉解释 */
class DeepDogStreamMqtt {
public:
    explicit DeepDogStreamMqtt(DeepDogMqttClient* client);
    ~DeepDogStreamMqtt();

    void SetVisionHub(VisionFrameHub* hub) { hub_ = hub; }
    void SetHttpServer(DeepDogHttpServer* http) { http_ = http; }
    void SetEnabled(bool enabled) { enabled_ = enabled; }

    void OnConnected();
    void OnDisconnected();
    void Stop();

    void OnMessage(const std::string& topic, const std::string& payload);
    bool PublishStatus(const char* error_override = nullptr);

private:
    static void PollTimerCb(void* arg);
    static void TakePhotoTask(void* arg);
    void EnqueueTakePhoto(const char* question);
    void PublishPhotoResult(bool ok, const std::string& result, const char* error, int elapsed_ms);
    void ApplyMode(VisionPublishMode mode);
    static const char* ProtocolModeStr(VisionPublishMode m);
    static bool ParseMode(const char* s, VisionPublishMode* out);

    DeepDogMqttClient* client_;
    VisionFrameHub* hub_ = nullptr;
    DeepDogHttpServer* http_ = nullptr;
    esp_timer_handle_t poll_timer_ = nullptr;
    bool enabled_ = false;

    std::string last_fingerprint_;
    std::string last_error_;
    std::atomic<bool> photo_busy_{false};
};
