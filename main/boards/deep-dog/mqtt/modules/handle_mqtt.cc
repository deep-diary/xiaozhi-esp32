#include "mqtt/modules/handle_mqtt.h"

#include "mqtt/mqtt_client.h"
#include "handle/handle_config.h"
#include "handle/sources/handle_bt.h"

#include <cJSON.h>
#include <cstring>
#include <esp_log.h>
#include <esp_timer.h>

#include <algorithm>
#include <ctime>

#define TAG "dog_mqtt_handle"

namespace {

int64_t UnixTs() {
    const time_t now = time(nullptr);
    if (now > 1000000000) {
        return static_cast<int64_t>(now);
    }
    return static_cast<int64_t>(esp_timer_get_time() / 1000000LL);
}

float Clamp11(float v) {
    if (v < -1.f) {
        return -1.f;
    }
    if (v > 1.f) {
        return 1.f;
    }
    return v;
}

float Clamp01(float v) {
    if (v < 0.f) {
        return 0.f;
    }
    if (v > 1.f) {
        return 1.f;
    }
    return v;
}

}  // namespace

DeepDogHandleMqtt::DeepDogHandleMqtt(DeepDogMqttClient* client) : client_(client) {}

DeepDogHandleMqtt::~DeepDogHandleMqtt() {
    Stop();
    if (status_flush_timer_) {
        esp_timer_delete(status_flush_timer_);
        status_flush_timer_ = nullptr;
    }
    if (timeout_timer_) {
        esp_timer_delete(timeout_timer_);
        timeout_timer_ = nullptr;
    }
}

void DeepDogHandleMqtt::EnsureTimeoutTimer() {
    if (timeout_timer_) {
        return;
    }
    esp_timer_create_args_t args = {
        .callback = &DeepDogHandleMqtt::TimeoutTimerCb,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "handle_in_to",
        .skip_unhandled_events = true,
    };
    if (esp_timer_create(&args, &timeout_timer_) != ESP_OK) {
        ESP_LOGE(TAG, "timeout timer create failed");
    }
}

void DeepDogHandleMqtt::EnsureStatusFlushTimer() {
    if (status_flush_timer_) {
        return;
    }
    esp_timer_create_args_t args = {
        .callback = &DeepDogHandleMqtt::StatusFlushTimerCb,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "handle_st_flush",
        .skip_unhandled_events = true,
    };
    if (esp_timer_create(&args, &status_flush_timer_) != ESP_OK) {
        ESP_LOGE(TAG, "status flush timer create failed");
    }
}

void DeepDogHandleMqtt::TimeoutTimerCb(void* arg) {
    auto* self = static_cast<DeepDogHandleMqtt*>(arg);
    if (self) {
        self->OnInputTimeout();
    }
}

void DeepDogHandleMqtt::StatusFlushTimerCb(void* arg) {
    auto* self = static_cast<DeepDogHandleMqtt*>(arg);
    if (self) {
        self->OnStatusFlush();
    }
}

void DeepDogHandleMqtt::ArmInputTimeout() {
#if DEEP_DOG_HANDLE_MQTT_INPUT_ENABLE
    EnsureTimeoutTimer();
    if (!timeout_timer_) {
        return;
    }
    esp_timer_stop(timeout_timer_);
    esp_timer_start_once(timeout_timer_,
                         static_cast<uint64_t>(DEEP_DOG_HANDLE_INPUT_TIMEOUT_MS) * 1000ULL);
#endif
}

void DeepDogHandleMqtt::ArmStatusFlush(int64_t delay_us) {
    EnsureStatusFlushTimer();
    if (!status_flush_timer_ || delay_us <= 0) {
        return;
    }
    esp_timer_stop(status_flush_timer_);
    esp_timer_start_once(status_flush_timer_, static_cast<uint64_t>(delay_us));
}

void DeepDogHandleMqtt::OnInputTimeout() {
    if (!hub_) {
        return;
    }
    HandleSnapshot clear{};
    clear.connected = false;
    clear.source = HandleSource::kWifi;
    clear.ts_us = esp_timer_get_time();
    ESP_LOGW(TAG, "handle/input timeout -> clear axes");
    hub_->Push(clear);
}

void DeepDogHandleMqtt::OnStatusFlush() {
    if (!status_pending_) {
        return;
    }
    status_pending_ = false;
    PublishStatus();
}

void DeepDogHandleMqtt::OnConnected() {
    connected_ = true;
    if (!enabled_ || !client_) {
        return;
    }
    client_->Subscribe("handle/cmd", 1);
#if DEEP_DOG_HANDLE_MQTT_INPUT_ENABLE
    client_->Subscribe("handle/input", 0);
#endif
    PublishStatus();
}

void DeepDogHandleMqtt::OnDisconnected() {
    connected_ = false;
    status_pending_ = false;
    if (status_flush_timer_) {
        esp_timer_stop(status_flush_timer_);
    }
    if (timeout_timer_) {
        esp_timer_stop(timeout_timer_);
    }
}

void DeepDogHandleMqtt::Stop() {
    OnDisconnected();
}

void DeepDogHandleMqtt::OnSnapshot(const HandleSnapshot& snap) {
    if (!enabled_ || !connected_) {
        return;
    }
    (void)snap;
    const int64_t now = esp_timer_get_time();
    const int64_t min_us = static_cast<int64_t>(DEEP_DOG_HANDLE_STATUS_MIN_INTERVAL_MS) * 1000LL;
    if (last_publish_us_ != 0 && (now - last_publish_us_) < min_us) {
        // 节流窗口内不丢最终态：到期后再发一次 Hub 最新快照
        status_pending_ = true;
        ArmStatusFlush(min_us - (now - last_publish_us_));
        return;
    }
    status_pending_ = false;
    if (status_flush_timer_) {
        esp_timer_stop(status_flush_timer_);
    }
    PublishStatus();
}

bool DeepDogHandleMqtt::ParseSnapshotJson(const std::string& payload, HandleSnapshot* out) {
    if (!out) {
        return false;
    }
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        return false;
    }

    HandleSnapshot s{};
    const cJSON* connected = cJSON_GetObjectItem(root, "connected");
    s.connected = cJSON_IsTrue(connected);

    const cJSON* source = cJSON_GetObjectItem(root, "source");
    if (cJSON_IsString(source) && source->valuestring) {
        s.source = HandleSourceFromName(source->valuestring);
    } else {
        s.source = HandleSource::kWifi;
    }

    const cJSON* axes = cJSON_GetObjectItem(root, "axes");
    if (cJSON_IsObject(axes)) {
        const cJSON* lx = cJSON_GetObjectItem(axes, "lx");
        const cJSON* ly = cJSON_GetObjectItem(axes, "ly");
        const cJSON* rx = cJSON_GetObjectItem(axes, "rx");
        const cJSON* ry = cJSON_GetObjectItem(axes, "ry");
        if (cJSON_IsNumber(lx)) {
            s.axes.lx = Clamp11(static_cast<float>(lx->valuedouble));
        }
        if (cJSON_IsNumber(ly)) {
            s.axes.ly = Clamp11(static_cast<float>(ly->valuedouble));
        }
        if (cJSON_IsNumber(rx)) {
            s.axes.rx = Clamp11(static_cast<float>(rx->valuedouble));
        }
        if (cJSON_IsNumber(ry)) {
            s.axes.ry = Clamp11(static_cast<float>(ry->valuedouble));
        }
    }

    const cJSON* buttons = cJSON_GetObjectItem(root, "buttons");
    if (cJSON_IsObject(buttons)) {
        auto getb = [&](const char* k) {
            const cJSON* it = cJSON_GetObjectItem(buttons, k);
            return cJSON_IsTrue(it);
        };
        auto getf = [&](const char* k) {
            const cJSON* it = cJSON_GetObjectItem(buttons, k);
            if (cJSON_IsNumber(it)) {
                return Clamp01(static_cast<float>(it->valuedouble));
            }
            if (cJSON_IsTrue(it)) {
                return 1.f;
            }
            return 0.f;
        };
        s.buttons.a = getb("a");
        s.buttons.b = getb("b");
        s.buttons.x = getb("x");
        s.buttons.y = getb("y");
        s.buttons.l1 = getb("l1");
        s.buttons.r1 = getb("r1");
        s.buttons.start = getb("start");
        s.buttons.select = getb("select");
        s.buttons.l2 = getf("l2");
        s.buttons.r2 = getf("r2");
        s.buttons.ps = getb("ps");
        s.buttons.l3 = getb("l3");
        s.buttons.r3 = getb("r3");
        s.buttons.touch = getb("touch");
        s.buttons.dpad_up = getb("dpad_up");
        s.buttons.dpad_down = getb("dpad_down");
        s.buttons.dpad_left = getb("dpad_left");
        s.buttons.dpad_right = getb("dpad_right");
    }

    const cJSON* touchpad = cJSON_GetObjectItem(root, "touchpad");
    if (cJSON_IsObject(touchpad)) {
        s.touchpad.present = true;
        const cJSON* active = cJSON_GetObjectItem(touchpad, "active");
        s.touchpad.active = cJSON_IsTrue(active);
        const cJSON* x = cJSON_GetObjectItem(touchpad, "x");
        const cJSON* y = cJSON_GetObjectItem(touchpad, "y");
        const cJSON* fingers = cJSON_GetObjectItem(touchpad, "fingers");
        if (cJSON_IsNumber(x)) {
            s.touchpad.x = Clamp01(static_cast<float>(x->valuedouble));
        }
        if (cJSON_IsNumber(y)) {
            s.touchpad.y = Clamp01(static_cast<float>(y->valuedouble));
        }
        if (cJSON_IsNumber(fingers)) {
            s.touchpad.fingers = fingers->valueint;
            if (s.touchpad.fingers < 0) {
                s.touchpad.fingers = 0;
            }
        }
        const cJSON* contacts = cJSON_GetObjectItem(touchpad, "contacts");
        if (cJSON_IsArray(contacts)) {
            const int n = cJSON_GetArraySize(contacts);
            s.touchpad.contact_count = 0;
            for (int i = 0; i < n && s.touchpad.contact_count < 2; ++i) {
                const cJSON* item = cJSON_GetArrayItem(contacts, i);
                if (!cJSON_IsObject(item)) {
                    continue;
                }
                HandleTouchContact& c = s.touchpad.contacts[s.touchpad.contact_count++];
                const cJSON* ca = cJSON_GetObjectItem(item, "active");
                c.active = cJSON_IsTrue(ca);
                const cJSON* cx = cJSON_GetObjectItem(item, "x");
                const cJSON* cy = cJSON_GetObjectItem(item, "y");
                if (cJSON_IsNumber(cx)) {
                    c.x = Clamp01(static_cast<float>(cx->valuedouble));
                }
                if (cJSON_IsNumber(cy)) {
                    c.y = Clamp01(static_cast<float>(cy->valuedouble));
                }
            }
            // 无主 x/y 时用 contacts[0] 回填
            if ((!cJSON_IsNumber(x) || !cJSON_IsNumber(y)) && s.touchpad.contact_count > 0) {
                for (int i = 0; i < s.touchpad.contact_count; ++i) {
                    if (s.touchpad.contacts[i].active) {
                        s.touchpad.x = s.touchpad.contacts[i].x;
                        s.touchpad.y = s.touchpad.contacts[i].y;
                        s.touchpad.active = true;
                        break;
                    }
                }
            }
            if (!cJSON_IsNumber(fingers)) {
                int n_active = 0;
                for (int i = 0; i < s.touchpad.contact_count; ++i) {
                    if (s.touchpad.contacts[i].active) {
                        ++n_active;
                    }
                }
                s.touchpad.fingers = n_active;
            }
        }
    }

    const cJSON* motion = cJSON_GetObjectItem(root, "motion");
    if (cJSON_IsObject(motion)) {
        s.motion.present = true;
        auto getmf = [&](const char* key) -> float {
            const cJSON* v = cJSON_GetObjectItem(motion, key);
            return cJSON_IsNumber(v) ? static_cast<float>(v->valuedouble) : 0.f;
        };
        s.motion.gyro_x = getmf("gyro_x");
        s.motion.gyro_y = getmf("gyro_y");
        s.motion.gyro_z = getmf("gyro_z");
        s.motion.accel_x = getmf("accel_x");
        s.motion.accel_y = getmf("accel_y");
        s.motion.accel_z = getmf("accel_z");
    }

    s.ts_us = esp_timer_get_time();
    cJSON_Delete(root);
    *out = s;
    return true;
}

void DeepDogHandleMqtt::HandleInput(const std::string& payload) {
#if !DEEP_DOG_HANDLE_MQTT_INPUT_ENABLE
    (void)payload;
    return;
#else
    if (!hub_) {
        return;
    }
    HandleSnapshot snap;
    if (!ParseSnapshotJson(payload, &snap)) {
        ESP_LOGW(TAG, "handle/input parse failed");
        return;
    }
    if (snap.source == HandleSource::kNone) {
        snap.source = HandleSource::kWifi;
    }
    hub_->Push(snap);
    if (snap.connected) {
        ArmInputTimeout();
    } else if (timeout_timer_) {
        esp_timer_stop(timeout_timer_);
    }
#endif
}

void DeepDogHandleMqtt::HandleCmd(const std::string& payload) {
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        return;
    }
    const cJSON* action = cJSON_GetObjectItem(root, "action");
    if (!cJSON_IsString(action) || !action->valuestring) {
        cJSON_Delete(root);
        return;
    }
    const char* a = action->valuestring;
    if (strcmp(a, "output") == 0) {
        // 设备也可能是发布方；下行回环 / 云端直发均忽略本地执行（I09 → PC 桥）
        cJSON_Delete(root);
        return;
    }
    if (strcmp(a, "enable") == 0) {
        if (hub_) {
            hub_->SetAppsEnabled(true);
        }
        ESP_LOGI(TAG, "handle apps enabled");
    } else if (strcmp(a, "disable") == 0) {
        if (hub_) {
            hub_->SetAppsEnabled(false);
        }
        ESP_LOGI(TAG, "handle apps disabled");
        // 联调钩：disable → 弱震 + 微红（I09）；桥在线时手柄应震一下
        (void)PublishOutput(200, 40, 0, 0.35f, 0.15f, 200);
    } else if (strcmp(a, "pair") == 0) {
        HandleBtStartPairing();
    } else if (strcmp(a, "rumble") == 0) {
        int duration_ms = 250;
        int weak = 128;
        int strong = 64;
        int delay_ms = 0;
        const cJSON* j = cJSON_GetObjectItem(root, "duration_ms");
        if (cJSON_IsNumber(j)) {
            duration_ms = j->valueint;
        }
        j = cJSON_GetObjectItem(root, "weak");
        if (cJSON_IsNumber(j)) {
            weak = j->valueint;
        }
        j = cJSON_GetObjectItem(root, "strong");
        if (cJSON_IsNumber(j)) {
            strong = j->valueint;
        }
        j = cJSON_GetObjectItem(root, "delay_ms");
        if (cJSON_IsNumber(j)) {
            delay_ms = j->valueint;
        }
        duration_ms = std::clamp(duration_ms, 0, 2000);
        weak = std::clamp(weak, 0, 255);
        strong = std::clamp(strong, 0, 255);
        delay_ms = std::clamp(delay_ms, 0, 2000);
        HandleBtRumble(static_cast<uint16_t>(delay_ms), static_cast<uint16_t>(duration_ms),
                       static_cast<uint8_t>(weak), static_cast<uint8_t>(strong));
        ESP_LOGI(TAG, "rumble delay=%d dur=%d weak=%d strong=%d", delay_ms, duration_ms, weak, strong);
    } else {
        ESP_LOGW(TAG, "unknown handle/cmd action=%s", a);
    }
    cJSON_Delete(root);
    PublishStatus();
}

bool DeepDogHandleMqtt::PublishOutput(int led_r, int led_g, int led_b, float rumble_strong,
                                      float rumble_weak, int duration_ms) {
    if (!enabled_ || !connected_ || !client_) {
        return false;
    }
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "action", "output");
    if (led_r >= 0 || led_g >= 0 || led_b >= 0) {
        cJSON* led = cJSON_CreateObject();
        cJSON_AddNumberToObject(led, "r", led_r >= 0 ? led_r : 0);
        cJSON_AddNumberToObject(led, "g", led_g >= 0 ? led_g : 0);
        cJSON_AddNumberToObject(led, "b", led_b >= 0 ? led_b : 0);
        cJSON_AddItemToObject(root, "led", led);
    }
    cJSON* rumble = cJSON_CreateObject();
    const float s = rumble_strong < 0.f ? 0.f : (rumble_strong > 1.f ? 1.f : rumble_strong);
    const float w = rumble_weak < 0.f ? 0.f : (rumble_weak > 1.f ? 1.f : rumble_weak);
    cJSON_AddNumberToObject(rumble, "strong", s);
    cJSON_AddNumberToObject(rumble, "weak", w);
    cJSON_AddItemToObject(root, "rumble", rumble);
    if (duration_ms > 0) {
        cJSON_AddNumberToObject(root, "duration_ms", duration_ms > 5000 ? 5000 : duration_ms);
    }
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(UnixTs()));

    char* printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return false;
    }
    const bool ok = client_->Publish("handle/cmd", printed, 1, false);
    cJSON_free(printed);
    if (ok) {
        ESP_LOGI(TAG, "published handle/cmd output rumble=%.2f/%.2f duration=%d", s, w,
                 duration_ms);
    }
    return ok;
}

void DeepDogHandleMqtt::OnMessage(const std::string& topic, const std::string& payload) {
    if (!enabled_ || !client_) {
        return;
    }
    if (topic == client_->Topic("handle/cmd")) {
        HandleCmd(payload);
        return;
    }
#if DEEP_DOG_HANDLE_MQTT_INPUT_ENABLE
    if (topic == client_->Topic("handle/input")) {
        HandleInput(payload);
    }
#endif
}

bool DeepDogHandleMqtt::PublishStatus() {
    if (!enabled_ || !connected_ || !client_) {
        return false;
    }

    HandleSnapshot snap{};
    if (hub_) {
        snap = hub_->GetSnapshot();
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "connected", snap.connected);
    if (const char* src = HandleSourceName(snap.source)) {
        cJSON_AddStringToObject(root, "source", src);
    }

    cJSON* axes = cJSON_CreateObject();
    cJSON_AddNumberToObject(axes, "lx", snap.axes.lx);
    cJSON_AddNumberToObject(axes, "ly", snap.axes.ly);
    cJSON_AddNumberToObject(axes, "rx", snap.axes.rx);
    cJSON_AddNumberToObject(axes, "ry", snap.axes.ry);
    cJSON_AddItemToObject(root, "axes", axes);

    cJSON* buttons = cJSON_CreateObject();
    cJSON_AddBoolToObject(buttons, "a", snap.buttons.a);
    cJSON_AddBoolToObject(buttons, "b", snap.buttons.b);
    cJSON_AddBoolToObject(buttons, "x", snap.buttons.x);
    cJSON_AddBoolToObject(buttons, "y", snap.buttons.y);
    cJSON_AddBoolToObject(buttons, "l1", snap.buttons.l1);
    cJSON_AddBoolToObject(buttons, "r1", snap.buttons.r1);
    cJSON_AddNumberToObject(buttons, "l2", snap.buttons.l2);
    cJSON_AddNumberToObject(buttons, "r2", snap.buttons.r2);
    cJSON_AddBoolToObject(buttons, "start", snap.buttons.start);
    cJSON_AddBoolToObject(buttons, "select", snap.buttons.select);
    cJSON_AddBoolToObject(buttons, "ps", snap.buttons.ps);
    cJSON_AddBoolToObject(buttons, "l3", snap.buttons.l3);
    cJSON_AddBoolToObject(buttons, "r3", snap.buttons.r3);
    cJSON_AddBoolToObject(buttons, "touch", snap.buttons.touch);
    cJSON_AddBoolToObject(buttons, "dpad_up", snap.buttons.dpad_up);
    cJSON_AddBoolToObject(buttons, "dpad_down", snap.buttons.dpad_down);
    cJSON_AddBoolToObject(buttons, "dpad_left", snap.buttons.dpad_left);
    cJSON_AddBoolToObject(buttons, "dpad_right", snap.buttons.dpad_right);
    cJSON_AddItemToObject(root, "buttons", buttons);

    if (snap.touchpad.present) {
        cJSON* touchpad = cJSON_CreateObject();
        cJSON_AddBoolToObject(touchpad, "active", snap.touchpad.active);
        cJSON_AddNumberToObject(touchpad, "x", snap.touchpad.x);
        cJSON_AddNumberToObject(touchpad, "y", snap.touchpad.y);
        cJSON_AddNumberToObject(touchpad, "fingers", snap.touchpad.fingers);
        if (snap.touchpad.contact_count > 0) {
            cJSON* contacts = cJSON_CreateArray();
            for (int i = 0; i < snap.touchpad.contact_count; ++i) {
                cJSON* item = cJSON_CreateObject();
                cJSON_AddBoolToObject(item, "active", snap.touchpad.contacts[i].active);
                cJSON_AddNumberToObject(item, "x", snap.touchpad.contacts[i].x);
                cJSON_AddNumberToObject(item, "y", snap.touchpad.contacts[i].y);
                cJSON_AddItemToArray(contacts, item);
            }
            cJSON_AddItemToObject(touchpad, "contacts", contacts);
        }
        cJSON_AddItemToObject(root, "touchpad", touchpad);
    }

    if (snap.motion.present) {
        cJSON* motion = cJSON_CreateObject();
        cJSON_AddNumberToObject(motion, "gyro_x", snap.motion.gyro_x);
        cJSON_AddNumberToObject(motion, "gyro_y", snap.motion.gyro_y);
        cJSON_AddNumberToObject(motion, "gyro_z", snap.motion.gyro_z);
        cJSON_AddNumberToObject(motion, "accel_x", snap.motion.accel_x);
        cJSON_AddNumberToObject(motion, "accel_y", snap.motion.accel_y);
        cJSON_AddNumberToObject(motion, "accel_z", snap.motion.accel_z);
        cJSON_AddItemToObject(root, "motion", motion);
    }

    cJSON_AddNumberToObject(root, "ts", static_cast<double>(UnixTs()));

    char* printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return false;
    }
    const bool ok = client_->Publish("handle/status", printed, 0, false);
    cJSON_free(printed);
    if (ok) {
        last_publish_us_ = esp_timer_get_time();
    }
    return ok;
}
