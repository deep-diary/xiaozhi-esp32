#pragma once

#include <string>

class DeepDogMqttClient;
class LedStripControl;

/** led/cmd 稀疏下行 + led/status retain 上行 */
class DeepDogLedMqtt {
public:
    explicit DeepDogLedMqtt(DeepDogMqttClient* client);
    ~DeepDogLedMqtt() = default;

    void SetEnabled(bool enabled) { enabled_ = enabled; }
    void SetControl(LedStripControl* ctrl);

    void OnConnected();
    void OnDisconnected();
    void Stop();

    void OnMessage(const std::string& topic, const std::string& payload);

    bool PublishStatus();

private:
    DeepDogMqttClient* client_;
    LedStripControl* control_ = nullptr;
    bool enabled_ = false;
    bool connected_ = false;
};
