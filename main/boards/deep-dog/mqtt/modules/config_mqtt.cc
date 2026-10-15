#include "mqtt/modules/config_mqtt.h"

#include "config_center/config_manager.h"
#include "mqtt/mqtt_client.h"
#include "mqtt/mqtt_config.h"
#include "mqtt/mqtt_topics.h"
#include "mqtt/modules/envelope.h"

#include <cJSON.h>
#include <esp_log.h>

#include <cstring>

#define TAG "dog_mqtt_cfg"

DeepDogConfigMqtt::DeepDogConfigMqtt(DeepDogMqttClient* client) : client_(client) {}

void DeepDogConfigMqtt::OnConnected() {
    if (client_) {
        client_->Subscribe(deep_dog_topics::Cmd("config"), 1);
    }
}

void DeepDogConfigMqtt::OnDisconnected() {}

void DeepDogConfigMqtt::Stop() {}

void DeepDogConfigMqtt::PublishAck(const char* action, const char* result) {
    if (!client_ || !client_->IsConnected()) {
        return;
    }
    auto& ccm = config_center::ConfigManager::GetInstance();
    cJSON* data = cJSON_CreateObject();
    if (!data) {
        return;
    }
    cJSON_AddStringToObject(data, "action", action ? action : "refresh");
    cJSON_AddStringToObject(data, "result", result ? result : "accepted");
    cJSON_AddNumberToObject(data, "config_version", ccm.config_version());
    cJSON_AddBoolToObject(data, "has_remote_config", ccm.has_remote_config());

    const std::string payload =
        deep_dog_envelope::Wrap(data, deep_dog_envelope::SchemaAck(),
                                DeepDogMqttConfig::MacCompactDeviceId());
    if (!payload.empty()) {
        client_->Publish(deep_dog_topics::Ack("config"), payload, 0, false);
    }
}

void DeepDogConfigMqtt::OnMessage(const std::string& topic, const std::string& payload) {
    if (!client_) {
        return;
    }
    if (topic != client_->Topic(deep_dog_topics::Cmd("config"))) {
        return;
    }

    cJSON* root = deep_dog_envelope::ParseBusiness(payload);
    if (!root) {
        ESP_LOGW(TAG, "config/cmd invalid_json");
        return;
    }
    const cJSON* action = cJSON_GetObjectItem(root, "action");
    const char* act = (cJSON_IsString(action) && action->valuestring) ? action->valuestring : "refresh";

    if (strcmp(act, "refresh") == 0) {
        config_center::ConfigManager::GetInstance().RequestSync("mqtt_notify");
        ESP_LOGI(TAG, "config/cmd refresh → schedule pull");
        cJSON_Delete(root);
        PublishAck("refresh", "accepted");
        return;
    }

    ESP_LOGW(TAG, "config/cmd unknown action=%s", act);
    cJSON_Delete(root);
    PublishAck(act, "unsupported");
}
