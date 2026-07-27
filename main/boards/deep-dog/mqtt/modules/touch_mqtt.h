#pragma once

#include "touch_btn/touch_event_hub.h"

#include <esp_timer.h>

#include <string>

class DeepDogMqttClient;
class TouchButtonController;
class TouchComboRecognizer;

/** touch/status 上行 + touch/cmd 阈值/标定 */
class DeepDogTouchMqtt {
public:
    explicit DeepDogTouchMqtt(DeepDogMqttClient* client);
    ~DeepDogTouchMqtt();

    void SetEnabled(bool enabled) { enabled_ = enabled; }
    void SetHub(TouchEventHub* hub) { hub_ = hub; }
    void SetController(TouchButtonController* ctrl) { ctrl_ = ctrl; }
    void SetComboRecognizer(TouchComboRecognizer* combo) { combo_ = combo; }

    void OnConnected();
    void OnDisconnected();
    void Stop();

    void OnMessage(const std::string& topic, const std::string& payload);

    /** Hub Push 时同步调用（与 dog 业务解耦） */
    void OnButtonEvent(const TouchEvent& ev);

    /** 组合命中后补发 retain 快照（含 last_combo） */
    void OnComboRecognized(const char* combo_id);

    bool PublishStatus();

private:
    static void PollTimerCb(void* arg);
    void EnsurePollTimer();
    void HandleCmd(const std::string& payload);

    DeepDogMqttClient* client_;
    TouchEventHub* hub_ = nullptr;
    TouchButtonController* ctrl_ = nullptr;
    TouchComboRecognizer* combo_ = nullptr;
    esp_timer_handle_t poll_timer_ = nullptr;
    bool enabled_ = false;
    bool connected_ = false;
    int last_calib_count_ = -1;
    bool last_calib_active_ = false;
};
