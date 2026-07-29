#pragma once

#include <esp_timer.h>

#include <string>

class DeepDogMqttClient;

/** pairing/status（retain）+ pairing/cmd；未绑定开机自动配对 */
class DeepDogPairingMqtt {
public:
    explicit DeepDogPairingMqtt(DeepDogMqttClient* client);
    ~DeepDogPairingMqtt();

    void OnConnected();
    void OnDisconnected();
    void OnMessage(const std::string& topic, const std::string& payload);
    void Stop();

    bool IsBound() const { return bound_; }
    const std::string& pair_code() const { return pair_code_; }

private:
    static void ReplayTimerCb(void* arg);

    void LoadBoundState();
    void SaveBoundState(bool bound);
    void EnsurePairCode();
    void PublishStatus();
    void AnnounceCode();
    void ApplyBound(bool bound);
    void EnsureReplayTimer();

    DeepDogMqttClient* client_;
    bool connected_ = false;
    bool bound_ = false;
    std::string pair_code_;
    esp_timer_handle_t replay_timer_ = nullptr;
};
