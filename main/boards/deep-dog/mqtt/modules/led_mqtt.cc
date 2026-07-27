#include "mqtt/modules/led_mqtt.h"

#include "mqtt/mqtt_client.h"
#include "led/led_strip_control.h"
#include "led/apps/led_app_dog.h"
#include "config.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <ctime>

#define TAG "dog_mqtt_led"

namespace {

int64_t UnixTs() {
    const time_t now = time(nullptr);
    if (now > 1000000000) {
        return static_cast<int64_t>(now);
    }
    return static_cast<int64_t>(esp_timer_get_time() / 1000000LL);
}

int ClampByte(int v) {
    if (v < 0) {
        return 0;
    }
    if (v > 255) {
        return 255;
    }
    return v;
}

}  // namespace

DeepDogLedMqtt::DeepDogLedMqtt(DeepDogMqttClient* client) : client_(client) {}

void DeepDogLedMqtt::SetControl(LedStripControl* ctrl) {
    control_ = ctrl;
    if (control_) {
        control_->SetStateChangedCallback([this]() {
            if (connected_) {
                PublishStatus();
            }
        });
    }
}

void DeepDogLedMqtt::OnConnected() {
    connected_ = true;
    if (!enabled_) {
        return;
    }
    if (client_) {
        client_->Subscribe("led/cmd", 1);
    }
    PublishStatus();
}

void DeepDogLedMqtt::OnDisconnected() {
    connected_ = false;
}

void DeepDogLedMqtt::Stop() {
    OnDisconnected();
}

void DeepDogLedMqtt::OnMessage(const std::string& topic, const std::string& payload) {
    if (!enabled_ || !client_) {
        return;
    }
    if (topic != client_->Topic("led/cmd")) {
        return;
    }

#if !DEEP_DOG_LED_ENABLE
    ESP_LOGW(TAG, "led/cmd ignored (LED disabled at compile)");
    return;
#else
    if (!control_ || !control_->ok()) {
        ESP_LOGW(TAG, "led/cmd ignored (no strip)");
        PublishStatus();
        return;
    }

    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        ESP_LOGW(TAG, "led/cmd invalid_json");
        return;
    }

    auto st = control_->GetStatus();
    bool touched = false;

    const cJSON* brightness = cJSON_GetObjectItem(root, "brightness");
    const cJSON* low_brightness = cJSON_GetObjectItem(root, "low_brightness");
    if (cJSON_IsNumber(brightness) || cJSON_IsNumber(low_brightness)) {
        uint8_t b = st.brightness;
        uint8_t lb = st.low_brightness;
        if (cJSON_IsNumber(brightness)) {
            b = static_cast<uint8_t>(ClampByte(static_cast<int>(brightness->valuedouble)));
        }
        if (cJSON_IsNumber(low_brightness)) {
            lb = static_cast<uint8_t>(ClampByte(static_cast<int>(low_brightness->valuedouble)));
        }
        control_->SetBrightness(b, lb);
        st = control_->GetStatus();
        touched = true;
    }

    const cJSON* r = cJSON_GetObjectItem(root, "r");
    const cJSON* g = cJSON_GetObjectItem(root, "g");
    const cJSON* b = cJSON_GetObjectItem(root, "b");
    if (cJSON_IsNumber(r)) {
        st.color.red = static_cast<uint8_t>(ClampByte(static_cast<int>(r->valuedouble)));
        touched = true;
    }
    if (cJSON_IsNumber(g)) {
        st.color.green = static_cast<uint8_t>(ClampByte(static_cast<int>(g->valuedouble)));
        touched = true;
    }
    if (cJSON_IsNumber(b)) {
        st.color.blue = static_cast<uint8_t>(ClampByte(static_cast<int>(b->valuedouble)));
        touched = true;
    }

    const cJSON* low_r = cJSON_GetObjectItem(root, "low_r");
    const cJSON* low_g = cJSON_GetObjectItem(root, "low_g");
    const cJSON* low_b = cJSON_GetObjectItem(root, "low_b");
    if (cJSON_IsNumber(low_r)) {
        st.low_color.red = static_cast<uint8_t>(ClampByte(static_cast<int>(low_r->valuedouble)));
        touched = true;
    }
    if (cJSON_IsNumber(low_g)) {
        st.low_color.green = static_cast<uint8_t>(ClampByte(static_cast<int>(low_g->valuedouble)));
        touched = true;
    }
    if (cJSON_IsNumber(low_b)) {
        st.low_color.blue = static_cast<uint8_t>(ClampByte(static_cast<int>(low_b->valuedouble)));
        touched = true;
    }

    const cJSON* interval = cJSON_GetObjectItem(root, "interval_ms");
    if (cJSON_IsNumber(interval)) {
        st.interval_ms = static_cast<int>(interval->valuedouble);
        if (st.interval_ms < 0) {
            st.interval_ms = 0;
        }
        touched = true;
    }

    const cJSON* scroll_length = cJSON_GetObjectItem(root, "scroll_length");
    if (cJSON_IsNumber(scroll_length)) {
        st.scroll_length = static_cast<int>(scroll_length->valuedouble);
        if (st.scroll_length < 1) {
            st.scroll_length = 1;
        }
        touched = true;
    }

    const cJSON* mode = cJSON_GetObjectItem(root, "mode");
    int mode_val = st.mode;
    bool mode_set = false;
    if (cJSON_IsNumber(mode)) {
        mode_val = static_cast<int>(mode->valuedouble);
        mode_set = true;
        touched = true;
    }

    cJSON_Delete(root);

    if (!touched) {
        ESP_LOGW(TAG, "led/cmd empty");
        return;
    }

    /* 无 mode 时：仅改亮度已处理；颜色等字段按当前 mode 重放效果 */
    if (!mode_set) {
        mode_val = st.mode;
        if (mode_val == DEEP_DOG_LED_MODE_SYSTEM) {
            /* 系统态下只改亮度，不强制重绑 */
            PublishStatus();
            return;
        }
    }

    switch (mode_val) {
        case DEEP_DOG_LED_MODE_OFF:
            control_->ApplyOff();
            break;
        case DEEP_DOG_LED_MODE_STATIC:
            control_->ApplyStatic(st.color);
            break;
        case DEEP_DOG_LED_MODE_BLINK:
            control_->ApplyBlink(st.color, st.interval_ms);
            break;
        case DEEP_DOG_LED_MODE_BREATHE:
            control_->ApplyBreathe(st.low_color, st.color, st.interval_ms > 0 ? st.interval_ms : 50);
            break;
        case DEEP_DOG_LED_MODE_SCROLL: {
            StripColor low = st.low_color;
            if (low.red == 0 && low.green == 0 && low.blue == 0) {
                low = {4, 4, 4};
            }
            control_->ApplyScroll(low, st.color, st.scroll_length,
                                  st.interval_ms > 0 ? st.interval_ms : 50);
            break;
        }
        case DEEP_DOG_LED_MODE_SYSTEM:
            LedAppDogOnEnterSystem(control_);
            break;
        default:
            ESP_LOGW(TAG, "led/cmd bad mode=%d", mode_val);
            PublishStatus();
            return;
    }

    ESP_LOGI(TAG, "led/cmd mode=%d rgb=%d,%d,%d", mode_val, st.color.red, st.color.green, st.color.blue);
    /* Apply* 已 Notify → PublishStatus；此处兜底 */
    PublishStatus();
#endif
}

bool DeepDogLedMqtt::PublishStatus() {
    if (!enabled_ || !client_ || !client_->IsConnected()) {
        return false;
    }

    cJSON* root = cJSON_CreateObject();
#if DEEP_DOG_LED_ENABLE
    if (control_) {
        const auto st = control_->GetStatus();
        cJSON_AddNumberToObject(root, "mode", st.mode);
        cJSON_AddNumberToObject(root, "brightness", st.brightness);
        cJSON_AddNumberToObject(root, "low_brightness", st.low_brightness);
        cJSON_AddNumberToObject(root, "r", st.color.red);
        cJSON_AddNumberToObject(root, "g", st.color.green);
        cJSON_AddNumberToObject(root, "b", st.color.blue);
        cJSON_AddNumberToObject(root, "low_r", st.low_color.red);
        cJSON_AddNumberToObject(root, "low_g", st.low_color.green);
        cJSON_AddNumberToObject(root, "low_b", st.low_color.blue);
        cJSON_AddNumberToObject(root, "interval_ms", st.interval_ms);
        cJSON_AddNumberToObject(root, "scroll_length", st.scroll_length);
        cJSON_AddNumberToObject(root, "led_count", st.led_count);
        cJSON_AddBoolToObject(root, "ok", st.ok);
    } else {
        cJSON_AddNumberToObject(root, "mode", 0);
        cJSON_AddNumberToObject(root, "brightness", 0);
        cJSON_AddNumberToObject(root, "r", 0);
        cJSON_AddNumberToObject(root, "g", 0);
        cJSON_AddNumberToObject(root, "b", 0);
        cJSON_AddBoolToObject(root, "ok", false);
    }
#else
    cJSON_AddNumberToObject(root, "mode", 0);
    cJSON_AddNumberToObject(root, "brightness", 0);
    cJSON_AddNumberToObject(root, "r", 0);
    cJSON_AddNumberToObject(root, "g", 0);
    cJSON_AddNumberToObject(root, "b", 0);
    cJSON_AddBoolToObject(root, "ok", false);
#endif
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(UnixTs()));

    char* printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return false;
    }
    const bool ok = client_->Publish("led/status", printed, 0, true);
    cJSON_free(printed);
    return ok;
}
