#include "mqtt/modules/touch_mqtt.h"

#include "mqtt/mqtt_client.h"
#include "touch_btn/touch_button_controller.h"
#include "touch_btn/touch_combo_recognizer.h"
#include "touch_btn/touch_config.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <ctime>

#define TAG "dog_mqtt_touch"

namespace {

int64_t UnixTs() {
    const time_t now = time(nullptr);
    if (now > 1000000000) {
        return static_cast<int64_t>(now);
    }
    return static_cast<int64_t>(esp_timer_get_time() / 1000000LL);
}

const char* LastEventStr(TouchButtonEvent e) {
    switch (e) {
        case TouchButtonEvent::kPress:
            return "press";
        case TouchButtonEvent::kRelease:
            return "release";
        case TouchButtonEvent::kLongPress:
            return "long_press";
        case TouchButtonEvent::kShortPress:
            return "short_press";
        case TouchButtonEvent::kDoubleClick:
            return "double_click";
    }
    return "release";
}

}  // namespace

DeepDogTouchMqtt::DeepDogTouchMqtt(DeepDogMqttClient* client) : client_(client) {}

void DeepDogTouchMqtt::OnConnected() {
    connected_ = true;
    if (!enabled_) {
        return;
    }
    PublishStatus();
}

void DeepDogTouchMqtt::OnDisconnected() {
    connected_ = false;
}

void DeepDogTouchMqtt::Stop() {
    OnDisconnected();
}

void DeepDogTouchMqtt::OnButtonEvent(const TouchEvent& ev) {
    (void)ev;
    if (!enabled_ || !connected_) {
        return;
    }
    PublishStatus();
}

void DeepDogTouchMqtt::OnComboRecognized(const char* combo_id) {
    ESP_LOGI(TAG, "combo for mqtt: %s", combo_id ? combo_id : "?");
    if (!enabled_ || !connected_) {
        return;
    }
    PublishStatus();
}

bool DeepDogTouchMqtt::PublishStatus() {
    if (!enabled_ || !client_ || !client_->IsConnected()) {
        return false;
    }

    TouchButtonState states[3];
    uint8_t mask = 0;
    bool have = false;
    if (hub_) {
        mask = hub_->GetPressedMask();
        for (int i = 0; i < 3; i++) {
            hub_->GetButtonState(i + 1, &states[i]);
        }
        have = true;
    } else if (ctrl_) {
        mask = ctrl_->GetPressedMask();
        for (int i = 0; i < 3; i++) {
            ctrl_->GetButtonState(i + 1, &states[i]);
        }
        have = true;
    } else {
        for (int i = 0; i < 3; i++) {
            states[i] = TouchButtonState{};
        }
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", have);
    cJSON_AddNumberToObject(root, "pressed_mask", mask);

    cJSON* buttons = cJSON_CreateArray();
    for (int i = 0; i < 3; i++) {
        cJSON* b = cJSON_CreateObject();
        cJSON_AddNumberToObject(b, "id", i + 1);
        cJSON_AddBoolToObject(b, "pressed", states[i].pressed);
        cJSON_AddBoolToObject(b, "long_press", states[i].long_press);
        cJSON_AddStringToObject(b, "last_event", LastEventStr(states[i].last_event));
        cJSON_AddItemToArray(buttons, b);
    }
    cJSON_AddItemToObject(root, "buttons", buttons);

#if DEEP_DOG_TOUCH_COMBO_ENABLE
    if (combo_ && combo_->LastComboId()) {
        cJSON* lc = cJSON_CreateObject();
        cJSON_AddStringToObject(lc, "id", combo_->LastComboId());
        cJSON_AddNumberToObject(lc, "ts", static_cast<double>(combo_->LastComboTsSec()));
        cJSON_AddItemToObject(root, "last_combo", lc);
    }
#endif

#if DEEP_DOG_TOUCH_MQTT_DEBUG
    cJSON* debug = cJSON_CreateArray();
    for (int i = 0; i < 3; i++) {
        cJSON* d = cJSON_CreateObject();
        cJSON_AddNumberToObject(d, "id", i + 1);
        cJSON_AddNumberToObject(d, "value", states[i].value);
        cJSON_AddNumberToObject(d, "baseline", states[i].baseline);
        cJSON_AddNumberToObject(d, "abs_diff", states[i].abs_diff);
        cJSON_AddItemToArray(debug, d);
    }
    cJSON_AddItemToObject(root, "debug", debug);
#endif

    cJSON_AddNumberToObject(root, "ts", static_cast<double>(UnixTs()));

    char* printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return false;
    }
    const bool pub_ok = client_->Publish("touch/status", printed, 0, true);
    ESP_LOGD(TAG, "touch/status mask=0x%02x pub=%d", (unsigned)mask, pub_ok ? 1 : 0);
    cJSON_free(printed);
    return pub_ok;
}
