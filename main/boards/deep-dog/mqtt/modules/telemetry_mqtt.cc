#include "mqtt/modules/telemetry_mqtt.h"

#include "mqtt/mqtt_client.h"
#include "mqtt/mqtt_config.h"
#include "mqtt/mqtt_topics.h"
#include "mqtt/modules/envelope.h"
#include "mqtt/datapoint_registry.h"
#include "net/deep_dog_sntp.h"

#include <cJSON.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_wifi.h>

#include <cstdio>
#include <ctime>
#include <string>

#define TAG "dog_mqtt_telem"

namespace {

std::string IsoTs(uint32_t unix_s) {
    time_t t = static_cast<time_t>(unix_s);
    struct tm tm_utc {};
    gmtime_r(&t, &tm_utc);
    char buf[40];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", tm_utc.tm_year + 1900, tm_utc.tm_mon + 1,
             tm_utc.tm_mday, tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec);
    return buf;
}

/** 从 deep-trace/{home}/{wu}/telemetry 反推 home / work_unit（缺省时兜底） */
void DeriveFromTopic(const std::string& topic, std::string* home, std::string* wu) {
    const size_t p1 = topic.find('/');
    if (p1 == std::string::npos) {
        return;
    }
    const size_t p2 = topic.find('/', p1 + 1);
    if (p2 == std::string::npos) {
        return;
    }
    const size_t p3 = topic.find('/', p2 + 1);
    if (home) {
        *home = topic.substr(p1 + 1, p2 - p1 - 1);
    }
    if (wu) {
        *wu = (p3 == std::string::npos) ? topic.substr(p2 + 1) : topic.substr(p2 + 1, p3 - p2 - 1);
    }
}

}  // namespace

DeepDogTelemetryMqtt::DeepDogTelemetryMqtt(DeepDogMqttClient* client) : client_(client) {
    esp_timer_create_args_t args = {
        .callback = &DeepDogTelemetryMqtt::TimerCb,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "dog_telemetry",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&args, &timer_);
}

DeepDogTelemetryMqtt::~DeepDogTelemetryMqtt() {
    Stop();
    if (timer_) {
        esp_timer_delete(timer_);
        timer_ = nullptr;
    }
}

void DeepDogTelemetryMqtt::ReloadConfig() {
    const DeepDogMqttSettings s = DeepDogMqttConfig::Load();
    home_code_ = s.home_code;
    work_unit_code_ = s.work_unit_code;
    device_program_ = s.device_program;
    // 跨端契约 01-mqtt-contract §5.5：遥测统一到 {prefix}/{device_id}/status/telemetry
    const std::string unified =
        DeepDogMqttConfig::TopicPrefix(s.device_id) + deep_dog_topics::Status("telemetry");
    // NVS 显式配置仅在"非旧式 deep-trace/{home}/{wu}/telemetry"时优先（联调覆盖）
    const bool legacy_shape =
        s.telemetry_topic.rfind("deep-trace/", 0) == 0 &&
        s.telemetry_topic.find("/telemetry") != std::string::npos &&
        s.telemetry_topic.find("/status/telemetry") == std::string::npos;
    topic_ = (!s.telemetry_topic.empty() && !legacy_shape) ? s.telemetry_topic : unified;
    enabled_ = !topic_.empty();
    if (enabled_ && (home_code_.empty() || work_unit_code_.empty())) {
        DeriveFromTopic(s.telemetry_topic, &home_code_, &work_unit_code_);
    }
}

void DeepDogTelemetryMqtt::TimerCb(void* arg) {
    auto* self = static_cast<DeepDogTelemetryMqtt*>(arg);
    if (self) {
        self->Publish();
    }
}

void DeepDogTelemetryMqtt::OnConnected() {
    ReloadConfig();
    if (!enabled_) {
        ESP_LOGI(TAG, "telemetry disabled (no telemetry_topic)");
        return;
    }
    ESP_LOGI(TAG, "telemetry topic=%s home=%s wu=%s", topic_.c_str(), home_code_.c_str(),
             work_unit_code_.c_str());
    Publish();
    if (timer_) {
        esp_timer_stop(timer_);
        esp_timer_start_periodic(timer_, DEEP_DOG_TELEMETRY_INTERVAL_US);
    }
}

void DeepDogTelemetryMqtt::OnDisconnected() {
    if (timer_) {
        esp_timer_stop(timer_);
    }
}

void DeepDogTelemetryMqtt::Stop() {
    OnDisconnected();
}

void DeepDogTelemetryMqtt::Publish() {
    if (!enabled_ || !client_ || !client_->IsConnected()) {
        return;
    }
    if (home_code_.empty() || work_unit_code_.empty()) {
        ESP_LOGW(TAG, "telemetry skipped: missing home/work_unit code");
        return;
    }

    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return;
    }
    cJSON_AddStringToObject(root, "home_code", home_code_.c_str());
    cJSON_AddStringToObject(root, "work_unit_code", work_unit_code_.c_str());
    cJSON_AddStringToObject(root, "device_program", device_program_.c_str());
    cJSON_AddStringToObject(root, "ts", IsoTs(DeepDogNowUnixSec()).c_str());

    // points 由数据点注册表自动汇入（见 docs/requirements/mqtt/modules/15-telemetry.md）
    cJSON* points = cJSON_AddObjectToObject(root, "points");
    const int n_points = DataPointRegistry::Instance().AppendTelemetryPoints(points);
    if (n_points <= 0) {
        ESP_LOGW(TAG, "telemetry points empty (no DP_CH_TELEMETRY data points)");
    }

    const std::string payload =
        deep_dog_envelope::Wrap(root, deep_dog_envelope::SchemaStatus(), client_->settings().device_id);
    if (payload.empty()) {
        return;
    }
    const bool ok = client_->PublishRaw(topic_, payload, 0, false);
    ESP_LOGI(TAG, "telemetry publish ok=%d topic=%s points=%d", ok ? 1 : 0, topic_.c_str(),
             n_points);
}
