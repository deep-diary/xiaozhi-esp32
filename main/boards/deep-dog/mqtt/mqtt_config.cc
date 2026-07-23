#include "mqtt/mqtt_config.h"

#include "settings.h"

#include <esp_log.h>
#include <esp_mac.h>

#include <cstdio>

#define TAG "dog_mqtt_cfg"

std::string DeepDogMqttConfig::TopicPrefix(const std::string& device_id) {
    return "deepdiary/deep-dog/" + device_id + "/";
}

std::string DeepDogMqttConfig::DefaultClientId(const std::string& device_id) {
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buf[48];
    snprintf(buf, sizeof(buf), "deep-dog-%s-%02x%02x%02x", device_id.c_str(), mac[3], mac[4], mac[5]);
    return buf;
}

DeepDogMqttSettings DeepDogMqttConfig::Load() {
    Settings settings("deep_dog_mqtt", false);
    DeepDogMqttSettings s;
    s.broker_host = settings.GetString("broker_host", DEEP_DOG_MQTT_DEFAULT_BROKER_HOST);
    s.broker_port = settings.GetInt("broker_port", DEEP_DOG_MQTT_DEFAULT_BROKER_PORT);
    s.device_id = settings.GetString("device_id", DEEP_DOG_MQTT_DEFAULT_DEVICE_ID);
    s.client_id = settings.GetString("client_id", "");
    s.username = settings.GetString("username", "");
    s.password = settings.GetString("password", "");
    s.keepalive_s = settings.GetInt("keepalive_s", DEEP_DOG_MQTT_DEFAULT_KEEPALIVE_S);
    if (s.device_id.empty()) {
        s.device_id = DEEP_DOG_MQTT_DEFAULT_DEVICE_ID;
    }
    if (s.client_id.empty()) {
        s.client_id = DefaultClientId(s.device_id);
    }
    if (s.broker_host.empty()) {
        s.broker_host = DEEP_DOG_MQTT_DEFAULT_BROKER_HOST;
    }
    if (s.broker_port <= 0) {
        s.broker_port = DEEP_DOG_MQTT_DEFAULT_BROKER_PORT;
    }
    ESP_LOGI(TAG, "broker %s:%d device_id=%s client_id=%s", s.broker_host.c_str(), s.broker_port,
             s.device_id.c_str(), s.client_id.c_str());
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
