#include "mqtt/modules/device_mqtt.h"

#include "mqtt/mqtt_client.h"
#include "mqtt/mqtt_config.h"
#include "config.h"
#include "system_info.h"

#include <wifi_manager.h>

#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>

#include <ctime>
#include <cstdio>
#include <string>

#define TAG "dog_mqtt_dev"

namespace {

constexpr size_t kLowInternalHeapBytes = 32 * 1024;

int64_t UnixTs() {
    const time_t now = time(nullptr);
    if (now > 1000000000) {
        return static_cast<int64_t>(now);
    }
    return static_cast<int64_t>(esp_timer_get_time() / 1000000LL);
}

std::string IsoTs(int64_t unix_s) {
    time_t t = static_cast<time_t>(unix_s);
    struct tm tm_utc {};
    gmtime_r(&t, &tm_utc);
    char buf[40];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", tm_utc.tm_year + 1900, tm_utc.tm_mon + 1,
             tm_utc.tm_mday, tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec);
    return buf;
}

std::string MacString() {
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4],
             mac[5]);
    return buf;
}

const char* ResetReasonString() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:
            return "poweron";
        case ESP_RST_EXT:
            return "external";
        case ESP_RST_SW:
            return "software";
        case ESP_RST_PANIC:
            return "panic";
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
            return "watchdog";
        case ESP_RST_DEEPSLEEP:
            return "deepsleep";
        case ESP_RST_BROWNOUT:
            return "brownout";
        case ESP_RST_SDIO:
            return "sdio";
        default:
            return "unknown";
    }
}

void AddMemBucket(cJSON* parent, const char* key, uint32_t caps) {
    cJSON* obj = cJSON_AddObjectToObject(parent, key);
    cJSON_AddNumberToObject(obj, "free", static_cast<double>(heap_caps_get_free_size(caps)));
    cJSON_AddNumberToObject(obj, "min", static_cast<double>(heap_caps_get_minimum_free_size(caps)));
    cJSON_AddNumberToObject(obj, "total", static_cast<double>(heap_caps_get_total_size(caps)));
}

}  // namespace

DeepDogDeviceMqtt::DeepDogDeviceMqtt(DeepDogMqttClient* client) : client_(client) {
    esp_timer_create_args_t args = {
        .callback = &DeepDogDeviceMqtt::StatusTimerCb,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "dog_dev_status",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&args, &status_timer_);
}

DeepDogDeviceMqtt::~DeepDogDeviceMqtt() {
    Stop();
    if (status_timer_) {
        esp_timer_delete(status_timer_);
        status_timer_ = nullptr;
    }
}

void DeepDogDeviceMqtt::StatusTimerCb(void* arg) {
    auto* self = static_cast<DeepDogDeviceMqtt*>(arg);
    if (self) {
        self->PublishStatus();
    }
}

void DeepDogDeviceMqtt::OnConnected() {
    PublishInfo();
    PublishStatus();
    if (status_timer_) {
        esp_timer_stop(status_timer_);
        esp_timer_start_periodic(status_timer_, DEEP_DOG_MQTT_STATUS_INTERVAL_US);
    }
}

void DeepDogDeviceMqtt::OnDisconnected() {
    if (status_timer_) {
        esp_timer_stop(status_timer_);
    }
}

void DeepDogDeviceMqtt::Stop() {
    OnDisconnected();
}

bool DeepDogDeviceMqtt::PublishInfo() {
    if (!client_ || !client_->IsConnected()) {
        return false;
    }
    const auto& s = client_->settings();
    const esp_app_desc_t* app = esp_app_get_description();
    const char* fw = (app && app->version[0] != '\0') ? app->version : "0.0.0";
    const std::string ip = WifiManager::GetInstance().GetIpAddress();

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device_id", s.device_id.c_str());
    cJSON_AddStringToObject(root, "firmware", fw);
    cJSON_AddStringToObject(root, "board", BOARD_NAME);
    cJSON_AddStringToObject(root, "chip_model", SystemInfo::GetChipModelName().c_str());
    if (app && app->idf_ver[0] != '\0') {
        cJSON_AddStringToObject(root, "idf_version", app->idf_ver);
    }
    cJSON_AddNumberToObject(root, "flash_size", static_cast<double>(SystemInfo::GetFlashSize()));
    cJSON_AddStringToObject(root, "mac", MacString().c_str());
    if (!ip.empty()) {
        cJSON_AddStringToObject(root, "ip", ip.c_str());
    }
    cJSON_AddNumberToObject(root, "http_port", http_port_);
    cJSON_AddStringToObject(root, "reset_reason", ResetReasonString());

    cJSON* power = cJSON_AddObjectToObject(root, "power");
    cJSON_AddBoolToObject(power, "supported", false);

    cJSON* ext_pins = cJSON_AddObjectToObject(root, "ext_pins");
    cJSON_AddStringToObject(ext_pins, "mode", DEEP_DOG_EXT_PIN_MODE_STR);
    cJSON_AddNumberToObject(ext_pins, "gpio_a", static_cast<double>(DEEP_DOG_EXT_PIN_A_GPIO));
    cJSON_AddNumberToObject(ext_pins, "gpio_b", static_cast<double>(DEEP_DOG_EXT_PIN_B_GPIO));

    cJSON* caps = cJSON_AddObjectToObject(root, "capabilities");
    cJSON_AddBoolToObject(caps, "dog", caps_.dog);
    cJSON_AddBoolToObject(caps, "motor", caps_.motor);
    cJSON_AddBoolToObject(caps, "stream", caps_.stream);
    cJSON_AddBoolToObject(caps, "face", caps_.face);
    cJSON_AddBoolToObject(caps, "track", caps_.track);
    cJSON_AddBoolToObject(caps, "imu", caps_.imu);
    cJSON_AddBoolToObject(caps, "led", caps_.led);
    cJSON_AddBoolToObject(caps, "servo", caps_.servo);
    cJSON_AddBoolToObject(caps, "gimbal", caps_.gimbal);
    cJSON_AddBoolToObject(caps, "handle", caps_.handle);
    cJSON_AddBoolToObject(caps, "touch", caps_.touch);
    cJSON_AddBoolToObject(caps, "can", caps_.can);
    cJSON_AddBoolToObject(caps, "arm", caps_.arm);
    cJSON_AddBoolToObject(caps, "uart", caps_.uart);
    const int64_t ts = UnixTs();
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(ts));
    cJSON_AddStringToObject(root, "ts_iso", IsoTs(ts).c_str());

    char* printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return false;
    }
    const bool ok = client_->Publish("device/info", printed, 0, true);
    ESP_LOGI(TAG, "device/info retain ok=%d", ok ? 1 : 0);
    cJSON_free(printed);
    return ok;
}

bool DeepDogDeviceMqtt::PublishStatus() {
    if (!client_ || !client_->IsConnected()) {
        return false;
    }
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "online", true);
    cJSON_AddNumberToObject(root, "uptime_s", static_cast<double>(esp_timer_get_time() / 1000000LL));
    cJSON_AddNumberToObject(root, "free_heap", static_cast<double>(esp_get_free_heap_size()));
    cJSON_AddNumberToObject(root, "min_free_heap", static_cast<double>(esp_get_minimum_free_heap_size()));

    cJSON* mem = cJSON_AddObjectToObject(root, "mem");
    AddMemBucket(mem, "internal", MALLOC_CAP_INTERNAL);
    AddMemBucket(mem, "psram", MALLOC_CAP_SPIRAM);

    wifi_ap_record_t ap{};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        cJSON_AddNumberToObject(root, "rssi", ap.rssi);
        cJSON_AddStringToObject(root, "wifi_ssid", reinterpret_cast<const char*>(ap.ssid));
        cJSON_AddNumberToObject(root, "wifi_channel", ap.primary);
    }

    const size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    cJSON* health = cJSON_AddObjectToObject(root, "health");
    cJSON* warn = cJSON_CreateArray();
    if (internal_free < kLowInternalHeapBytes) {
        cJSON_AddItemToArray(warn, cJSON_CreateString("low_internal_heap"));
    }
    cJSON_AddBoolToObject(health, "ok", cJSON_GetArraySize(warn) == 0);
    cJSON_AddItemToObject(health, "warn", warn);

    const int64_t ts = UnixTs();
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(ts));
    cJSON_AddStringToObject(root, "ts_iso", IsoTs(ts).c_str());

    char* printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return false;
    }
    const bool ok = client_->Publish("device/status", printed, 0, false);
    cJSON_free(printed);
    return ok;
}
