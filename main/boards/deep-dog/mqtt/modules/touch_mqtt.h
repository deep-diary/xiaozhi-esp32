#pragma once

#include "touch_btn/touch_event_hub.h"

class DeepDogMqttClient;
class TouchButtonController;
class TouchComboRecognizer;

/** touch/status 上行 QoS0 retain；on_button_event / combo 发三键快照 */
class DeepDogTouchMqtt {
public:
    explicit DeepDogTouchMqtt(DeepDogMqttClient* client);
    ~DeepDogTouchMqtt() = default;

    void SetEnabled(bool enabled) { enabled_ = enabled; }
    void SetHub(TouchEventHub* hub) { hub_ = hub; }
    void SetController(TouchButtonController* ctrl) { ctrl_ = ctrl; }
    void SetComboRecognizer(TouchComboRecognizer* combo) { combo_ = combo; }

    void OnConnected();
    void OnDisconnected();
    void Stop();

    /** Hub Push 时同步调用（与 dog 业务解耦） */
    void OnButtonEvent(const TouchEvent& ev);

    /** 组合命中后补发 retain 快照（含 last_combo） */
    void OnComboRecognized(const char* combo_id);

    bool PublishStatus();

private:
    DeepDogMqttClient* client_;
    TouchEventHub* hub_ = nullptr;
    TouchButtonController* ctrl_ = nullptr;
    TouchComboRecognizer* combo_ = nullptr;
    bool enabled_ = false;
    bool connected_ = false;
};
