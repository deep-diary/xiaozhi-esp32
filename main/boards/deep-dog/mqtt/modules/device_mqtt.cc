#include "mqtt/modules/device_mqtt.h"

#include "mqtt/mqtt_client.h"
#include "mqtt/mqtt_config.h"

#include <wifi_manager.h>

#include <cJSON.h>
#include <esp_app_desc.h>
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
    cJSON_AddStringToObject(root, "mac", MacString().c_str());
    if (!ip.empty()) {
        cJSON_AddStringToObject(root, "ip", ip.c_str());
    }
    cJSON_AddNumberToObject(root, "http_port", http_port_);

    cJSON* caps = cJSON_AddObjectToObject(root, "capabilities");
    cJSON_AddBoolToObject(caps, "dog", caps_.dog);
    cJSON_AddBoolToObject(caps, "stream", caps_.stream);
    cJSON_AddBoolToObject(caps, "face", caps_.face);
    cJSON_AddBoolToObject(caps, "imu", caps_.imu);
    cJSON_AddBoolToObject(caps, "led", caps_.led);
    cJSON_AddBoolToObject(caps, "servo", caps_.servo);
    cJSON_AddBoolToObject(caps, "gimbal", caps_.gimbal);
    cJSON_AddBoolToObject(caps, "handle", caps_.handle);
    cJSON_AddBoolToObject(caps, "touch", caps_.touch);
    cJSON_AddBoolToObject(caps, "can", caps_.can);
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

    wifi_ap_record_t ap{};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        cJSON_AddNumberToObject(root, "rssi", ap.rssi);
    }
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
