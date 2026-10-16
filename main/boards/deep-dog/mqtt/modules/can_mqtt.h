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

    /** 注册 can 数据点到统一 Registry（Phase 4；需访问模块内部状态） */
    void RegisterDataPoints();

private:
    static void FlushTimerCb(void* arg);
    static void HeartbeatTimerCb(void* arg);
    static void FrameListener(const CanFrame* frame, int is_tx, void* ctx);

    void OnFrame(const CanFrame* frame, bool is_tx);
    void FlushFrames();
    void EmitHeartbeat();
    /** 采样总线链路质量（rx_fps 等），≥1s 才重算，见 UpdateLinkStats */
    void UpdateLinkStats();
    void ApplyCmd(const char* json);
    void HandleTx(const char* json);

    DeepDogMqttClient* client_;
    bool enabled_ = false;
    bool connected_ = false;

    // 联调剖面默认全开（tunnel + 网页注入 + 不过滤标准帧）；量产须改回，见 12-can.md
    bool tunnel_ = true;
    bool mirror_tx_ = true;
    bool allow_tx_ = true;
    bool ext_only_ = false;
    int max_hz_ = 100;
    int batch_max_ = 32;
    uint32_t dropped_ = 0;
    uint32_t hb_seq_ = 0;

    /** 链路质量采样（rx_fps）：上一次的累计 RX 帧数与采样时刻 */
    uint32_t last_rx_frames_ = 0;
    int64_t last_stats_us_ = 0;
    double rx_fps_ = 0.0;

    void* flush_timer_ = nullptr;
    void* heartbeat_timer_ = nullptr;
    void* batch_mutex_ = nullptr;
    cJSON* pending_frames_ = nullptr;
    int64_t last_flush_us_ = 0;
};
