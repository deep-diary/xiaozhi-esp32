#include "mqtt/modules/touch_mqtt.h"

#include "mqtt/mqtt_client.h"
#include "touch_btn/touch_button_controller.h"
#include "touch_btn/touch_combo_recognizer.h"
#include "touch_btn/touch_config.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <cstring>
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

const char* CalibPhaseStr(TouchCalibPhase p) {
    switch (p) {
        case TouchCalibPhase::kIdle:
            return "idle";
        case TouchCalibPhase::kCollect:
            return "collect";
        case TouchCalibPhase::kDone:
            return "done";
        case TouchCalibPhase::kFail:
            return "fail";
    }
    return "idle";
}

}  // namespace

DeepDogTouchMqtt::DeepDogTouchMqtt(DeepDogMqttClient* client) : client_(client) {}

DeepDogTouchMqtt::~DeepDogTouchMqtt() {
    Stop();
    if (poll_timer_) {
        esp_timer_delete(poll_timer_);
        poll_timer_ = nullptr;
    }
}

void DeepDogTouchMqtt::EnsurePollTimer() {
    if (poll_timer_) {
        return;
    }
    esp_timer_create_args_t args = {
        .callback = &DeepDogTouchMqtt::PollTimerCb,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "touch_mqtt_poll",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&args, &poll_timer_);
}

void DeepDogTouchMqtt::PollTimerCb(void* arg) {
    auto* self = static_cast<DeepDogTouchMqtt*>(arg);
    if (!self || !self->enabled_ || !self->connected_ || !self->ctrl_) {
        return;
    }
    const TouchCalibStatus st = self->ctrl_->GetCalibStatus();
    const bool want_debug = self->ctrl_->WantMqttDebug();
    if (st.active || want_debug || st.count != self->last_calib_count_ ||
        st.active != self->last_calib_active_) {
        self->last_calib_count_ = st.count;
        self->last_calib_active_ = st.active;
        self->PublishStatus();
    }
}

void DeepDogTouchMqtt::OnConnected() {
    connected_ = true;
    if (!enabled_) {
        return;
    }
    if (client_) {
        client_->Subscribe("touch/cmd", 1);
    }
    EnsurePollTimer();
    if (poll_timer_) {
        esp_timer_stop(poll_timer_);
        esp_timer_start_periodic(poll_timer_, 250000);  // 250ms during session
    }
    last_calib_count_ = -1;
    PublishStatus();
}

void DeepDogTouchMqtt::OnDisconnected() {
    connected_ = false;
    if (poll_timer_) {
        esp_timer_stop(poll_timer_);
    }
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

void DeepDogTouchMqtt::OnMessage(const std::string& topic, const std::string& payload) {
    if (!enabled_ || !client_) {
        return;
    }
    if (topic != client_->Topic("touch/cmd")) {
        return;
    }
    HandleCmd(payload);
}

void DeepDogTouchMqtt::HandleCmd(const std::string& payload) {
    if (!ctrl_) {
        ESP_LOGW(TAG, "touch/cmd ignored (no controller)");
        return;
    }
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        ESP_LOGW(TAG, "touch/cmd invalid_json");
        return;
    }

    const cJSON* dbg = cJSON_GetObjectItem(root, "debug");
    if (cJSON_IsBool(dbg)) {
        ctrl_->SetMqttDebug(cJSON_IsTrue(dbg));
    }

    const cJSON* action = cJSON_GetObjectItem(root, "action");
    if (!cJSON_IsString(action) || !action->valuestring) {
        cJSON_Delete(root);
        PublishStatus();
        return;
    }

    const char* act = action->valuestring;
    bool persist = true;
    const cJSON* persist_j = cJSON_GetObjectItem(root, "persist");
    if (cJSON_IsBool(persist_j)) {
        persist = cJSON_IsTrue(persist_j);
    }

    if (strcmp(act, "set_thresholds") == 0) {
        TouchThresholds thr = ctrl_->GetThresholds();
        const cJSON* ratio = cJSON_GetObjectItem(root, "press_abs_ratio");
        if (cJSON_IsNumber(ratio)) {
            thr.press_abs_ratio = static_cast<float>(ratio->valuedouble);
        }
        const cJSON* rratio = cJSON_GetObjectItem(root, "release_abs_ratio");
        if (cJSON_IsNumber(rratio)) {
            thr.release_abs_ratio = static_cast<float>(rratio->valuedouble);
        }
        const cJSON* po = cJSON_GetObjectItem(root, "press_abs_offset");
        if (cJSON_IsNumber(po)) {
            thr.press_abs_offset = static_cast<uint32_t>(po->valuedouble);
        }
        const cJSON* ro = cJSON_GetObjectItem(root, "release_abs_offset");
        if (cJSON_IsNumber(ro)) {
            thr.release_abs_offset = static_cast<uint32_t>(ro->valuedouble);
        }
        const cJSON* deb = cJSON_GetObjectItem(root, "debounce_cycles");
        if (cJSON_IsNumber(deb)) {
            thr.debounce_cycles = static_cast<uint8_t>(deb->valuedouble);
        }
        const cJSON* ba = cJSON_GetObjectItem(root, "baseline_alpha");
        if (cJSON_IsNumber(ba)) {
            thr.baseline_alpha = static_cast<float>(ba->valuedouble);
        }
        const cJSON* buttons = cJSON_GetObjectItem(root, "buttons");
        if (cJSON_IsArray(buttons)) {
            const int n = cJSON_GetArraySize(buttons);
            for (int i = 0; i < n; i++) {
                const cJSON* b = cJSON_GetArrayItem(buttons, i);
                if (!cJSON_IsObject(b)) {
                    continue;
                }
                const cJSON* id = cJSON_GetObjectItem(b, "id");
                if (!cJSON_IsNumber(id)) {
                    continue;
                }
                const int bid = static_cast<int>(id->valuedouble);
                if (bid < 1 || bid > 3) {
                    continue;
                }
                const cJSON* pmin = cJSON_GetObjectItem(b, "press_abs_min");
                if (cJSON_IsNumber(pmin)) {
                    thr.press_abs_min[bid - 1] = static_cast<uint32_t>(pmin->valuedouble);
                }
                const cJSON* rmin = cJSON_GetObjectItem(b, "release_abs_min");
                if (cJSON_IsNumber(rmin)) {
                    thr.release_abs_min[bid - 1] = static_cast<uint32_t>(rmin->valuedouble);
                }
            }
        }
        const bool ok = ctrl_->SetThresholds(thr, persist);
        ESP_LOGI(TAG, "touch/cmd set_thresholds ok=%d persist=%d", ok ? 1 : 0, persist ? 1 : 0);
    } else if (strcmp(act, "calibrate") == 0) {
        int button_id = 3;
        int samples = DEEP_DOG_TOUCH_CALIB_DEFAULT_SAMPLES;
        const cJSON* bid = cJSON_GetObjectItem(root, "button_id");
        if (cJSON_IsNumber(bid)) {
            button_id = static_cast<int>(bid->valuedouble);
        }
        const cJSON* sm = cJSON_GetObjectItem(root, "samples");
        if (cJSON_IsNumber(sm)) {
            samples = static_cast<int>(sm->valuedouble);
        }
        const bool ok = ctrl_->StartCalibrate(button_id, samples);
        ESP_LOGI(TAG, "touch/cmd calibrate btn=%d samples=%d ok=%d", button_id, samples, ok ? 1 : 0);
    } else if (strcmp(act, "reset_thresholds") == 0) {
        const bool ok = ctrl_->ResetThresholdsToFactory(persist);
        ESP_LOGI(TAG, "touch/cmd reset_thresholds ok=%d", ok ? 1 : 0);
    } else if (strcmp(act, "cancel_calibrate") == 0) {
        ctrl_->CancelCalibrate();
        ESP_LOGI(TAG, "touch/cmd cancel_calibrate");
    } else {
        ESP_LOGW(TAG, "touch/cmd unknown action=%s", act);
    }

    cJSON_Delete(root);
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

    // Prefer live raw from controller when available (calib/debug)
    if (ctrl_) {
        for (int i = 0; i < 3; i++) {
            TouchButtonState cs{};
            if (ctrl_->GetButtonState(i + 1, &cs)) {
                states[i].value = cs.value;
                states[i].baseline = cs.baseline;
                states[i].abs_diff = cs.abs_diff;
                // keep pressed/last_event from hub if present
                if (!hub_) {
                    states[i] = cs;
                } else {
                    states[i].pressed = cs.pressed;
                    states[i].long_press = cs.long_press;
                    states[i].last_event = cs.last_event;
                }
            }
        }
        mask = ctrl_->GetPressedMask();
        have = true;
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

    if (ctrl_) {
        const TouchThresholds thr = ctrl_->GetThresholds();
        cJSON* th = cJSON_CreateObject();
        cJSON_AddNumberToObject(th, "press_abs_ratio", thr.press_abs_ratio);
        cJSON_AddNumberToObject(th, "release_abs_ratio", thr.release_abs_ratio);
        cJSON_AddNumberToObject(th, "press_abs_offset", thr.press_abs_offset);
        cJSON_AddNumberToObject(th, "release_abs_offset", thr.release_abs_offset);
        cJSON_AddNumberToObject(th, "debounce_cycles", thr.debounce_cycles);
        cJSON_AddNumberToObject(th, "baseline_alpha", thr.baseline_alpha);
        cJSON_AddNumberToObject(th, "baseline_update_abs_ratio", thr.baseline_update_abs_ratio);
        cJSON_AddNumberToObject(th, "baseline_update_abs_offset", thr.baseline_update_abs_offset);
        cJSON* tb = cJSON_CreateArray();
        for (int i = 0; i < 3; i++) {
            cJSON* b = cJSON_CreateObject();
            cJSON_AddNumberToObject(b, "id", i + 1);
            cJSON_AddNumberToObject(b, "press_abs_min", thr.press_abs_min[i]);
            cJSON_AddNumberToObject(b, "release_abs_min", thr.release_abs_min[i]);
            cJSON_AddItemToArray(tb, b);
        }
        cJSON_AddItemToObject(th, "buttons", tb);
        cJSON_AddItemToObject(root, "thresholds", th);

        const TouchCalibStatus st = ctrl_->GetCalibStatus();
        cJSON* calib = cJSON_CreateObject();
        cJSON_AddBoolToObject(calib, "active", st.active);
        cJSON_AddNumberToObject(calib, "button_id", st.button_id);
        cJSON_AddStringToObject(calib, "phase", CalibPhaseStr(st.phase));
        cJSON_AddNumberToObject(calib, "count", st.count);
        cJSON_AddNumberToObject(calib, "samples", st.samples);
        if (st.error) {
            cJSON_AddStringToObject(calib, "error", st.error);
        }
        cJSON_AddItemToObject(root, "calib", calib);
    }

#if DEEP_DOG_TOUCH_COMBO_ENABLE
    if (combo_ && combo_->LastComboId()) {
        cJSON* lc = cJSON_CreateObject();
        cJSON_AddStringToObject(lc, "id", combo_->LastComboId());
        cJSON_AddNumberToObject(lc, "ts", static_cast<double>(combo_->LastComboTsSec()));
        cJSON_AddItemToObject(root, "last_combo", lc);
    }
#endif

    const bool force_debug = ctrl_ && ctrl_->WantMqttDebug();
#if DEEP_DOG_TOUCH_MQTT_DEBUG
    const bool include_debug = true;
#else
    const bool include_debug = force_debug;
#endif
    if (include_debug) {
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
    }

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
