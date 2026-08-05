#pragma once

#include <cstdint>
#include <string>

#include "can/ESP32-TWAI-CAN.hpp"

struct cJSON;

class DeepDogMqttClient;

/** can/cmd|tx ↓ + can/status|frames ↑（MOT-02 / 12-can） */
class DeepDogCanMqtt {
public:
    explicit DeepDogCanMqtt(DeepDogMqttClient* client);
    ~DeepDogCanMqtt();

    void SetEnabled(bool enabled) { enabled_ = enabled; }

    void OnConnected();
    void OnDisconnected();
    void Stop();
    void OnMessage(const std::string& topic, const std::string& payload);

    bool PublishStatus(bool force = true);

private:
    static void FlushTimerCb(void* arg);
    static void FrameListener(const CanFrame* frame, int is_tx, void* ctx);

    void OnFrame(const CanFrame* frame, bool is_tx);
    void FlushFrames();
    void ApplyCmd(const char* json);
    void HandleTx(const char* json);

    DeepDogMqttClient* client_;
    bool enabled_ = false;
    bool connected_ = false;

    bool tunnel_ = false;
    bool mirror_tx_ = true;
    bool allow_tx_ = false;
    bool ext_only_ = true;
    int max_hz_ = 50;
    int batch_max_ = 32;
    uint32_t dropped_ = 0;

    void* flush_timer_ = nullptr;
    void* batch_mutex_ = nullptr;
    cJSON* pending_frames_ = nullptr;
    int64_t last_flush_us_ = 0;
};
