#pragma once

#include <esp_timer.h>

#include <string>

class DeepDogMqttClient;

/** pairing/status + pairing/cmd + pairing/request；显式触发配对（非开机自动） */
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

    /** MCP / 长按1+轻触2：未绑定进入配对；已绑定语音提示 */
    void StartPairingSessionOrAnnounceBound();

    /** MCP / 长按1+轻触3：上行 pairing/request unbind */
    void RequestUnbind();

private:
    static void ReplayTimerCb(void* arg);

    void LoadBoundState();
    void SaveBoundState(bool bound);
    void EnsurePairCode();
    void PublishStatus();
    void PublishPairingRequest(const char* action);
    void AnnounceCode();
    void ShowPairingAlert();
    void ShowAlreadyBoundAlert();
    void ShowNotBoundAlert();
    void ShowBoundSuccessAlert();
    void ShowUnboundAlert();
    void ShowUnbindRequestSentAlert();
    void ApplyBound(bool bound);
    void EnsureReplayTimer();
    void StopReplayTimer();
    void StartPairingSession(bool announce);
    bool ShouldPublishPairCode() const;

    DeepDogMqttClient* client_;
    bool connected_ = false;
    bool bound_ = false;
    bool pro_pairing_mqtt_ = false;
    bool session_active_ = false;
    std::string pair_code_;
    esp_timer_handle_t replay_timer_ = nullptr;
};
