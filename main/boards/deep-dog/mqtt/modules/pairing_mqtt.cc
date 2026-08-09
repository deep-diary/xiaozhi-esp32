#include "mqtt/modules/pairing_mqtt.h"

#include "mqtt/mqtt_client.h"
#include "mqtt/mqtt_config.h"
#include "application.h"
#include "assets/lang_config.h"
#include "settings.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <esp_timer.h>

#include <array>
#include <cstdio>
#include <ctime>
#include <string>

#define TAG "dog_mqtt_pair"

namespace {

constexpr int64_t kReplayIntervalUs = 45LL * 1000 * 1000;

int64_t UnixTs() {
    const time_t now = time(nullptr);
    if (now > 1000000000) {
        return static_cast<int64_t>(now);
    }
    return static_cast<int64_t>(esp_timer_get_time() / 1000000LL);
}

std::string MacDisplay() {
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4],
             mac[5]);
    return buf;
}

std::string GenerateSixDigitCode() {
    const uint32_t n = esp_random() % 1000000U;
    char buf[8];
    snprintf(buf, sizeof(buf), "%06u", static_cast<unsigned>(n));
    return buf;
}

}  // namespace

DeepDogPairingMqtt::DeepDogPairingMqtt(DeepDogMqttClient* client) : client_(client) {
    LoadBoundState();
}

DeepDogPairingMqtt::~DeepDogPairingMqtt() {
    Stop();
    if (replay_timer_) {
        esp_timer_delete(replay_timer_);
        replay_timer_ = nullptr;
    }
}

void DeepDogPairingMqtt::LoadBoundState() {
    Settings settings("deep_dog_mqtt", false);
    bound_ = settings.GetBool("bound", false);
    pair_code_ = settings.GetString("pair_code", "");
    pro_pairing_mqtt_ = settings.GetBool("pro_pairing_mqtt", false);
}

void DeepDogPairingMqtt::SaveBoundState(bool bound) {
    Settings settings("deep_dog_mqtt", true);
    settings.SetBool("bound", bound);
    if (bound) {
        settings.SetString("device_id", DeepDogMqttConfig::MacCompactDeviceId());
        settings.EraseKey("pair_code");
        pair_code_.clear();
        session_active_ = false;
    } else {
        settings.EraseKey("device_id");
        session_active_ = false;
    }
    bound_ = bound;
}

void DeepDogPairingMqtt::SetIdentityReloadCallback(std::function<void()> cb) {
    identity_reload_cb_ = std::move(cb);
}

void DeepDogPairingMqtt::EnsurePairCode() {
    if (pair_code_.size() == 6) {
        bool ok = true;
        for (char c : pair_code_) {
            if (c < '0' || c > '9') {
                ok = false;
                break;
            }
        }
        if (ok) {
            return;
        }
    }
    pair_code_ = GenerateSixDigitCode();
    Settings settings("deep_dog_mqtt", true);
    settings.SetString("pair_code", pair_code_);
    ESP_LOGI(TAG, "generated pair code %s", pair_code_.c_str());
}

bool DeepDogPairingMqtt::ShouldPublishPairCode() const {
    return !bound_ && (session_active_ || pro_pairing_mqtt_);
}

void DeepDogPairingMqtt::EnsureReplayTimer() {
    if (replay_timer_) {
        return;
    }
    esp_timer_create_args_t args = {
        .callback = &DeepDogPairingMqtt::ReplayTimerCb,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "pair_replay",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&args, &replay_timer_);
}

void DeepDogPairingMqtt::StopReplayTimer() {
    if (replay_timer_) {
        esp_timer_stop(replay_timer_);
    }
}

void DeepDogPairingMqtt::ReplayTimerCb(void* arg) {
    auto* self = static_cast<DeepDogPairingMqtt*>(arg);
    if (!self || !self->connected_ || self->bound_ || !self->ShouldPublishPairCode()) {
        return;
    }
    self->PublishStatus();
    if (self->session_active_) {
        self->AnnounceCode();
    }
}

void DeepDogPairingMqtt::ShowPairingAlert() {
    char msg[64];
    snprintf(msg, sizeof(msg), "请在网页输入配对码：%s", pair_code_.c_str());
    Application::GetInstance().Alert("配对", msg, "link", Lang::Sounds::OGG_POPUP);
}

void DeepDogPairingMqtt::ShowAlreadyBoundAlert() {
    Application::GetInstance().Alert("设备", "本设备已绑定", "check", Lang::Sounds::OGG_SUCCESS);
}

void DeepDogPairingMqtt::ShowNotBoundAlert() {
    Application::GetInstance().Alert("设备", "当前未绑定", "neutral", std::string_view{});
}

void DeepDogPairingMqtt::ShowBoundSuccessAlert() {
    Application::GetInstance().Alert("配对", "绑定成功", "happy", Lang::Sounds::OGG_SUCCESS);
}

void DeepDogPairingMqtt::ShowUnboundAlert() {
    Application::GetInstance().Alert("配对", "已解绑，可重新配对", "neutral", Lang::Sounds::OGG_POPUP);
}

void DeepDogPairingMqtt::ShowUnbindRequestSentAlert() {
    Application::GetInstance().Alert("配对", "解绑请求已发送", "link", Lang::Sounds::OGG_POPUP);
}

void DeepDogPairingMqtt::AnnounceCode() {
    if (pair_code_.empty()) {
        return;
    }
    ESP_LOGI(TAG, "announce pair code %s", pair_code_.c_str());
    struct DigitSound {
        char digit;
        const std::string_view& sound;
    };
    static const std::array<DigitSound, 10> digit_sounds{{
        DigitSound{'0', Lang::Sounds::OGG_0},
        DigitSound{'1', Lang::Sounds::OGG_1},
        DigitSound{'2', Lang::Sounds::OGG_2},
        DigitSound{'3', Lang::Sounds::OGG_3},
        DigitSound{'4', Lang::Sounds::OGG_4},
        DigitSound{'5', Lang::Sounds::OGG_5},
        DigitSound{'6', Lang::Sounds::OGG_6},
        DigitSound{'7', Lang::Sounds::OGG_7},
        DigitSound{'8', Lang::Sounds::OGG_8},
        DigitSound{'9', Lang::Sounds::OGG_9},
    }};
    auto& app = Application::GetInstance();
    for (char digit : pair_code_) {
        for (const auto& ds : digit_sounds) {
            if (ds.digit == digit) {
                app.PlaySound(ds.sound);
                break;
            }
        }
    }
}

void DeepDogPairingMqtt::PublishStatus() {
    if (!client_ || !connected_) {
        return;
    }
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return;
    }
    const std::string device_id = DeepDogMqttConfig::MacCompactDeviceId();
    const std::string mac = MacDisplay();
    cJSON_AddStringToObject(root, "device_id", device_id.c_str());
    cJSON_AddStringToObject(root, "mac", mac.c_str());
    cJSON_AddBoolToObject(root, "bound", bound_);
    if (ShouldPublishPairCode() && !pair_code_.empty()) {
        cJSON_AddStringToObject(root, "code", pair_code_.c_str());
    }
    const int64_t ts = UnixTs();
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(ts));
    char* raw = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!raw) {
        return;
    }
    client_->Publish("pairing/status", raw, 0, true);
    cJSON_free(raw);
}

void DeepDogPairingMqtt::PublishPairingRequest(const char* action) {
    if (!client_ || !connected_ || !action) {
        ESP_LOGW(TAG, "pairing/request skipped (not connected)");
        return;
    }
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return;
    }
    const std::string device_id = DeepDogMqttConfig::MacCompactDeviceId();
    cJSON_AddStringToObject(root, "action", action);
    cJSON_AddStringToObject(root, "device_id", device_id.c_str());
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(UnixTs()));
    char* raw = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!raw) {
        return;
    }
    client_->Publish("pairing/request", raw, 1, false);
    cJSON_free(raw);
    ESP_LOGI(TAG, "pairing/request action=%s", action);
}

void DeepDogPairingMqtt::StartPairingSession(bool announce) {
    if (bound_) {
        ShowAlreadyBoundAlert();
        return;
    }
    session_active_ = true;
    EnsurePairCode();
    PublishStatus();
    if (announce) {
        ShowPairingAlert();
        AnnounceCode();
    }
    EnsureReplayTimer();
    if (replay_timer_) {
        esp_timer_start_periodic(replay_timer_, kReplayIntervalUs);
    }
}

void DeepDogPairingMqtt::StartPairingSessionOrAnnounceBound() {
    LoadBoundState();
    if (bound_) {
        ShowAlreadyBoundAlert();
        return;
    }
    StartPairingSession(true);
}

void DeepDogPairingMqtt::RequestUnbind() {
    LoadBoundState();
    if (!bound_) {
        ShowNotBoundAlert();
        return;
    }
    if (!connected_) {
        Application::GetInstance().Alert("配对", "网络未连接，请稍后重试", "sad", std::string_view{});
        return;
    }
    PublishPairingRequest("unbind");
    ShowUnbindRequestSentAlert();
}

void DeepDogPairingMqtt::ApplyBound(bool bound) {
    const bool was_bound = bound_;
    SaveBoundState(bound);
    StopReplayTimer();

    // 不可在 MQTT 任务回调里 Stop/Start 客户端（会 Cache panic）；defer 到主循环
    Application::GetInstance().Schedule([this, bound, was_bound]() {
        if (identity_reload_cb_) {
            identity_reload_cb_();
        }
        PublishStatus();
        if (!bound) {
            if (was_bound) {
                ShowUnboundAlert();
            }
            ESP_LOGI(TAG, "device unbound");
            return;
        }
        ShowBoundSuccessAlert();
        ESP_LOGI(TAG, "device bound");
    });
}

void DeepDogPairingMqtt::OnConnected() {
    connected_ = true;
    LoadBoundState();
    if (client_) {
        client_->Subscribe("pairing/cmd", 1);
    }

    if (bound_) {
        PublishStatus();
        return;
    }

    if (pro_pairing_mqtt_) {
        EnsurePairCode();
        PublishStatus();
        EnsureReplayTimer();
        if (replay_timer_) {
            esp_timer_start_periodic(replay_timer_, kReplayIntervalUs);
        }
        ESP_LOGI(TAG, "pro pairing mqtt: retain code without announce");
    } else {
        ESP_LOGI(TAG, "unbound idle (pair on MCP/touch)");
    }
}

void DeepDogPairingMqtt::OnDisconnected() {
    connected_ = false;
    StopReplayTimer();
}

void DeepDogPairingMqtt::Stop() {
    OnDisconnected();
}

void DeepDogPairingMqtt::OnMessage(const std::string& topic, const std::string& payload) {
    if (!client_ || topic != client_->Topic("pairing/cmd")) {
        return;
    }
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        ESP_LOGW(TAG, "pairing/cmd bad json");
        return;
    }
    const cJSON* action = cJSON_GetObjectItem(root, "action");
    if (!cJSON_IsString(action) || !action->valuestring) {
        cJSON_Delete(root);
        return;
    }
    const std::string act = action->valuestring;
    cJSON_Delete(root);
    if (act == "bound") {
        ApplyBound(true);
    } else if (act == "unbind") {
        ApplyBound(false);
    } else if (act == "start_pairing") {
        ESP_LOGI(TAG, "pairing/cmd start_pairing");
        StartPairingSessionOrAnnounceBound();
    } else {
        ESP_LOGW(TAG, "pairing/cmd unknown action %s", act.c_str());
    }
}
