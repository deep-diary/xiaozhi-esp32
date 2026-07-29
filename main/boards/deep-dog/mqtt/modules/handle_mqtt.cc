#include "mqtt/modules/handle_mqtt.h"

#include "mqtt/mqtt_client.h"
#include "handle/handle_config.h"

#include <cJSON.h>
#include <cstring>
#include <esp_log.h>
#include <esp_timer.h>

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

void DeepDogHandleMqtt::TimeoutTimerCb(void* arg) {
    auto* self = static_cast<DeepDogHandleMqtt*>(arg);
    if (self) {
        self->OnInputTimeout();
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
    const int64_t now = esp_timer_get_time();
    const int64_t min_us = static_cast<int64_t>(DEEP_DOG_HANDLE_STATUS_MIN_INTERVAL_MS) * 1000LL;
    if (last_publish_us_ != 0 && (now - last_publish_us_) < min_us) {
        return;
    }
    (void)snap;
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
    } else if (strcmp(a, "pair") == 0) {
#if DEEP_DOG_HANDLE_BT_ENABLE
        ESP_LOGI(TAG, "pair requested (BT path)");
#else
        ESP_LOGW(TAG, "pair ignored (HANDLE_BT disabled)");
#endif
    } else {
        ESP_LOGW(TAG, "unknown handle/cmd action=%s", a);
    }
    cJSON_Delete(root);
    PublishStatus();
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
    cJSON_AddItemToObject(root, "buttons", buttons);

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
