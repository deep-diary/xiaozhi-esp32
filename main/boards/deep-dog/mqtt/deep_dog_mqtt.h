#pragma once

#include "mqtt/mqtt_config.h"

#include <memory>
#include <string>

class VisionFrameHub;
class DeepDogHttpServer;
class DeepDogImuSensor;
class DeepDogImuSwitch;
class TouchEventHub;
class TouchButtonController;
class TouchComboRecognizer;
class LedStripControl;

/**
 * deep-dog 板级 MQTT 门面：device + stream + imu + touch + led …
 * StartNetwork 之后调用 Start()。
 */
class DeepDogMqtt {
public:
    DeepDogMqtt();
    ~DeepDogMqtt();

    void SetVisionHub(VisionFrameHub* hub);
    void SetHttpServer(DeepDogHttpServer* http);
    void SetHttpPort(int port);
    void SetImuSensor(DeepDogImuSensor* sensor);
    void SetImuSwitch(DeepDogImuSwitch* hub);
    void SetTouchHub(TouchEventHub* hub);
    void SetTouchController(TouchButtonController* ctrl);
    void SetTouchComboRecognizer(TouchComboRecognizer* combo);
    /** 组合命中后补发 touch/status（含 last_combo） */
    void NotifyTouchCombo(const char* combo_id);
    void SetLedControl(LedStripControl* ctrl);

    bool Start();
    void Stop();
    bool IsRunning() const;

    /** 配对：未绑定进入会话；已绑定屏显提示 */
    void StartPairingSessionOrAnnounceBound();
    /** 设备发起解绑（pairing/request） */
    void RequestDeviceUnbind();
    bool IsDeviceBound() const;
    const std::string& DevicePairCode() const;

private:
#if DEEP_DOG_MQTT_ENABLE
    struct Impl;
    std::unique_ptr<Impl> impl_;
#endif
};
