#include "mqtt/mqtt_config.h"

#include "settings.h"
#include "vision/vision_config.h"

#include <esp_log.h>
#include <esp_mac.h>

#include <cstdio>

#define TAG "dog_mqtt_cfg"

std::string DeepDogMqttConfig::TopicPrefix(const std::string& device_id) {
    return "deepdiary/deep-dog/" + device_id + "/";
}

std::string DeepDogMqttConfig::MacCompactDeviceId() {
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buf[13];
    snprintf(buf, sizeof(buf), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4],
             mac[5]);
    return buf;
}

std::string DeepDogMqttConfig::DefaultClientId(const std::string& device_id) {
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buf[48];
    snprintf(buf, sizeof(buf), "deep-dog-%s-%02x%02x%02x", device_id.c_str(), mac[3], mac[4], mac[5]);
    return buf;
}

std::string DeepDogMqttConfig::EffectiveDeviceId(bool bound, const std::string& nvs_override) {
    if (!nvs_override.empty()) {
        return nvs_override;
    }
    if (bound) {
        return MacCompactDeviceId();
    }
    return DEEP_DOG_MQTT_DEFAULT_DEVICE_ID;
}

std::string DeepDogMqttConfig::StreamPathForDeviceId(const std::string& device_id) {
    return std::string("deep-dog/") + device_id;
}

std::string DeepDogMqttConfig::RtspPushUrlForDeviceId(const std::string& device_id) {
    char url[160];
    snprintf(url, sizeof(url), "rtsp://%s:%u/%s", DEEP_DOG_VISION_RTSP_HOST,
             static_cast<unsigned>(DEEP_DOG_VISION_RTSP_PORT),
             StreamPathForDeviceId(device_id).c_str());
    return url;
}

std::string DeepDogMqttConfig::PublicHlsUrlForDeviceId(const std::string& device_id) {
    char url[192];
    snprintf(url, sizeof(url), "https://live.deep-diary.com/%s/index.m3u8",
             StreamPathForDeviceId(device_id).c_str());
    return url;
}

std::string DeepDogMqttConfig::LanHlsUrlForDeviceId(const std::string& device_id) {
    char url[192];
    snprintf(url, sizeof(url), "http://%s:8888/%s/index.m3u8", DEEP_DOG_VISION_RTSP_HOST,
             StreamPathForDeviceId(device_id).c_str());
    return url;
}

DeepDogMqttSettings DeepDogMqttConfig::Load() {
    Settings settings("deep_dog_mqtt", false);
    DeepDogMqttSettings s;
    s.broker_host = settings.GetString("broker_host", DEEP_DOG_MQTT_DEFAULT_BROKER_HOST);
    s.broker_port = settings.GetInt("broker_port", DEEP_DOG_MQTT_DEFAULT_BROKER_PORT);
    const bool bound = settings.GetBool("bound", false);
    const std::string nvs_device_id = settings.GetString("device_id", "");
    s.device_id = EffectiveDeviceId(bound, nvs_device_id);
    s.client_id = settings.GetString("client_id", "");
    s.username = settings.GetString("username", "");
    s.password = settings.GetString("password", "");
    s.keepalive_s = settings.GetInt("keepalive_s", DEEP_DOG_MQTT_DEFAULT_KEEPALIVE_S);
    if (s.client_id.empty()) {
        s.client_id = DefaultClientId(s.device_id);
    }
    if (s.broker_host.empty()) {
        s.broker_host = DEEP_DOG_MQTT_DEFAULT_BROKER_HOST;
    }
    if (s.broker_port <= 0) {
        s.broker_port = DEEP_DOG_MQTT_DEFAULT_BROKER_PORT;
    }
    // 联调：NVS 仍是公共站 / 旧家宽 EMQX 时，跟编译期默认迁到本机 broker
    const bool lab_default =
        std::string(DEEP_DOG_MQTT_DEFAULT_BROKER_HOST) != "broker.emqx.io";
    if (lab_default &&
        (s.broker_host == "broker.emqx.io" || s.broker_host == "192.168.31.25")) {
        ESP_LOGW(TAG, "migrate broker_host %s → %s", s.broker_host.c_str(),
                 DEEP_DOG_MQTT_DEFAULT_BROKER_HOST);
        s.broker_host = DEEP_DOG_MQTT_DEFAULT_BROKER_HOST;
        s.broker_port = DEEP_DOG_MQTT_DEFAULT_BROKER_PORT;
        Settings writable("deep_dog_mqtt", true);
        writable.SetString("broker_host", s.broker_host);
        writable.SetInt("broker_port", s.broker_port);
    }
    ESP_LOGI(TAG, "broker %s:%d bound=%d device_id=%s client_id=%s", s.broker_host.c_str(),
             s.broker_port, bound ? 1 : 0, s.device_id.c_str(), s.client_id.c_str());
    return s;
}

void DeepDogMqttConfig::Save(const DeepDogMqttSettings& s) {
    Settings settings("deep_dog_mqtt", true);
    settings.SetString("broker_host", s.broker_host);
    settings.SetInt("broker_port", s.broker_port);
    settings.SetString("device_id", s.device_id);
    settings.SetString("client_id", s.client_id);
    settings.SetString("username", s.username);
    settings.SetString("password", s.password);
    settings.SetInt("keepalive_s", s.keepalive_s);
    ESP_LOGI(TAG, "saved to NVS");
}
