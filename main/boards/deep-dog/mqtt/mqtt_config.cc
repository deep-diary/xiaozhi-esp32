#include "mqtt/mqtt_config.h"

#include "config_center/config_manager.h"
#include "settings.h"
#include "vision/vision_config.h"

#include <esp_log.h>
#include <esp_mac.h>
#include <esp_random.h>

#include <cstdio>

#define TAG "dog_mqtt_cfg"

namespace {
/** 运行期生效的 topic 前缀根（Load/SetTopicRoot 更新） */
std::string g_topic_root = DEEP_DOG_MQTT_DEFAULT_TOPIC_ROOT;
}  // namespace

std::string DeepDogMqttConfig::TopicRoot() { return g_topic_root; }

void DeepDogMqttConfig::SetTopicRoot(const std::string& root) {
    g_topic_root = root.empty() ? DEEP_DOG_MQTT_DEFAULT_TOPIC_ROOT : root;
}

std::string DeepDogMqttConfig::TopicPrefix(const std::string& device_id) {
    // 统一骨架：{prefix}/{device_id}/（跨端契约 01-mqtt-contract）
    return g_topic_root + "/" + device_id + "/";
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
    // deep-dog-{device_id}-{随机}：随机后缀避免同 device_id 会话互踢（swrs/mqtt/modules/00-pairing）
    char buf[64];
    snprintf(buf, sizeof(buf), "deep-dog-%s-%08lx", device_id.c_str(),
             static_cast<unsigned long>(esp_random()));
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
    s.topic_root = settings.GetString("topic_root", DEEP_DOG_MQTT_DEFAULT_TOPIC_ROOT);
    if (s.topic_root.empty()) {
        s.topic_root = DEEP_DOG_MQTT_DEFAULT_TOPIC_ROOT;
    }
    SetTopicRoot(s.topic_root);
    const bool bound = settings.GetBool("bound", false);
    const std::string nvs_device_id = settings.GetString("device_id", "");
    s.device_id = EffectiveDeviceId(bound, nvs_device_id);
    s.client_id = settings.GetString("client_id", "");
    s.username = settings.GetString("username", "");
    s.password = settings.GetString("password", "");
    s.keepalive_s = settings.GetInt("keepalive_s", DEEP_DOG_MQTT_DEFAULT_KEEPALIVE_S);
    s.use_tls = settings.GetInt("broker_tls", DEEP_DOG_MQTT_DEFAULT_TLS) != 0;
    s.server_api_base = settings.GetString("server_api_base", DEEP_DOG_MQTT_DEFAULT_SERVER_API_BASE);
    s.telemetry_topic = settings.GetString("telemetry_topic", "");
    s.home_code = settings.GetString("home_code", "");
    s.work_unit_code = settings.GetString("work_unit_code", "");
    s.device_program = settings.GetString("device_program", DEEP_DOG_TELEMETRY_DEVICE_PROGRAM);
    if (s.device_program.empty()) {
        s.device_program = DEEP_DOG_TELEMETRY_DEVICE_PROGRAM;
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
    // 仅迁移明确废弃的旧联调地址；勿改用户手填的 broker.emqx.io（配网页 N03）
    if (s.broker_host == "192.168.31.25" || s.broker_host == "192.168.3.73") {
        ESP_LOGW(TAG, "migrate broker_host %s → %s", s.broker_host.c_str(),
                 DEEP_DOG_MQTT_DEFAULT_BROKER_HOST);
        s.broker_host = DEEP_DOG_MQTT_DEFAULT_BROKER_HOST;
        s.broker_port = DEEP_DOG_MQTT_DEFAULT_BROKER_PORT;
        Settings writable("deep_dog_mqtt", true);
        writable.SetString("broker_host", s.broker_host);
        writable.SetInt("broker_port", s.broker_port);
    }
    // [CFG-01] 配置中心优先：若已从服务器/缓存加载，覆盖旧 NVS 值
    {
        auto& ccm = config_center::ConfigManager::GetInstance();
        if (ccm.config_version() > 0 || ccm.has_remote_config()) {
            const auto& m = ccm.config().mqtt;
            if (!m.broker_host.empty()) {
                s.broker_host = m.broker_host;
            }
            if (m.broker_port > 0) {
                s.broker_port = m.broker_port;
            }
            s.use_tls = m.use_tls;
            if (!m.username.empty()) {
                s.username = m.username;
            }
            if (!m.password.empty()) {
                s.password = m.password;
            }
            if (!m.topic_root.empty()) {
                s.topic_root = m.topic_root;
                SetTopicRoot(s.topic_root);
            }
            if (m.keepalive_s > 0) {
                s.keepalive_s = m.keepalive_s;
            }
            const auto& a = ccm.config().api_server;
            if (!a.base_url.empty()) {
                s.server_api_base = a.base_url;
            }
        }
    }
    ESP_LOGI(TAG, "broker %s:%d tls=%d bound=%d device_id=%s server_api_base=%s telemetry=%s", s.broker_host.c_str(),
             s.broker_port, s.use_tls ? 1 : 0, bound ? 1 : 0, s.device_id.c_str(), s.server_api_base.c_str(),
             s.telemetry_topic.c_str());
    return s;
}

void DeepDogMqttConfig::Save(const DeepDogMqttSettings& s) {
    Settings settings("deep_dog_mqtt", true);
    settings.SetString("broker_host", s.broker_host);
    settings.SetInt("broker_port", s.broker_port);
    settings.SetInt("broker_tls", s.use_tls ? 1 : 0);
    settings.SetString("topic_root", s.topic_root);
    settings.SetString("device_id", s.device_id);
    settings.SetString("client_id", s.client_id);
    settings.SetString("username", s.username);
    settings.SetString("password", s.password);
    settings.SetInt("keepalive_s", s.keepalive_s);
    settings.SetString("server_api_base", s.server_api_base);
    settings.SetString("telemetry_topic", s.telemetry_topic);
    settings.SetString("home_code", s.home_code);
    settings.SetString("work_unit_code", s.work_unit_code);
    settings.SetString("device_program", s.device_program);
    ESP_LOGI(TAG, "saved to NVS");
}

void DeepDogMqttConfig::SaveBoundEndpoint(const std::string& broker_host, int broker_port, bool use_tls,
                                          const std::string& telemetry_topic, const std::string& home_code,
                                          const std::string& work_unit_code) {
    Settings settings("deep_dog_mqtt", true);
    if (!broker_host.empty()) {
        settings.SetString("broker_host", broker_host);
        settings.SetInt("broker_port", broker_port > 0 ? broker_port : DEEP_DOG_MQTT_DEFAULT_BROKER_PORT);
        settings.SetInt("broker_tls", use_tls ? 1 : 0);
    }
    if (!telemetry_topic.empty()) {
        settings.SetString("telemetry_topic", telemetry_topic);
    }
    if (!home_code.empty()) {
        settings.SetString("home_code", home_code);
    }
    if (!work_unit_code.empty()) {
        settings.SetString("work_unit_code", work_unit_code);
    }
    ESP_LOGI(TAG, "bound endpoint saved broker=%s:%d tls=%d telemetry=%s", broker_host.c_str(), broker_port,
             use_tls ? 1 : 0, telemetry_topic.c_str());
}
