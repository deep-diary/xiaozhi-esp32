#pragma once

#include "handle/handle_event_hub.h"

#include <esp_timer.h>

#include <string>

class DeepDogMqttClient;

/** handle/status ↑ + handle/input ↓ + handle/cmd ↓ */
class DeepDogHandleMqtt {
public:
    explicit DeepDogHandleMqtt(DeepDogMqttClient* client);
    ~DeepDogHandleMqtt();

    void SetEnabled(bool enabled) { enabled_ = enabled; }
    void SetHub(HandleEventHub* hub) { hub_ = hub; }

    void OnConnected();
    void OnDisconnected();
    void Stop();

    void OnMessage(const std::string& topic, const std::string& payload);

    /** Hub Push 同步回调（可节流发布） */
    void OnSnapshot(const HandleSnapshot& snap);

    bool PublishStatus();
    bool PublishKeymap();

    /**
     * 发布 handle/cmd action=output（I09）。PC 桥订同一 Topic 写 DS4 灯/震。
     * led_* <0 表示省略该通道；rumble ∈ [0,1]；duration_ms<=0 表示不带时长字段。
     */
    bool PublishOutput(int led_r, int led_g, int led_b, float rumble_strong, float rumble_weak,
                       int duration_ms);

private:
    static void TimeoutTimerCb(void* arg);
    static void StatusFlushTimerCb(void* arg);
    void EnsureTimeoutTimer();
    void EnsureStatusFlushTimer();
    void ArmInputTimeout();
    void ArmStatusFlush(int64_t delay_us);
    void OnInputTimeout();
    void OnStatusFlush();
    void HandleCmd(const std::string& payload);
    void HandleInput(const std::string& payload);
    bool ParseSnapshotJson(const std::string& payload, HandleSnapshot* out);

    DeepDogMqttClient* client_;
    HandleEventHub* hub_ = nullptr;
    esp_timer_handle_t timeout_timer_ = nullptr;
    esp_timer_handle_t status_flush_timer_ = nullptr;
    bool enabled_ = false;
    bool connected_ = false;
    bool status_pending_ = false;
    int64_t last_publish_us_ = 0;
};
