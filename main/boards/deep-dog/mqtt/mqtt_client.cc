#include "mqtt/mqtt_client.h"

#include <mqtt_client.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>

#include <atomic>
#include <cstring>

#define TAG "dog_mqtt"

namespace {

constexpr EventBits_t kConnectedBit = BIT0;
constexpr EventBits_t kErrorBit = BIT1;
/** 须覆盖 network.timeout_ms，避免应用先超时、mqtt 任务仍在连时被并发 destroy */
constexpr int kNetworkTimeoutMs = 20000;
constexpr int kConnectTimeoutMs = kNetworkTimeoutMs + 5000;
/** 断连后尽快重连；过长会导致手柄/云台长时间失控 */
constexpr int kReconnectIntervalUs = 3 * 1000 * 1000;
constexpr int kMaxReconnect = 0;  // 0 = unlimited
constexpr int kMqttBufferSize = 4096;
constexpr uint32_t kReconnectTaskStack = 6144;
constexpr UBaseType_t kReconnectTaskPrio = 5;

}  // namespace

struct DeepDogMqttClient::Impl {
    esp_mqtt_client_handle_t handle = nullptr;
    EventGroupHandle_t events = nullptr;
    esp_timer_handle_t reconnect_timer = nullptr;
    SemaphoreHandle_t mutex = nullptr;
    std::atomic<bool> connected{false};
    std::atomic<bool> stopping{false};
    std::atomic<bool> connect_in_progress{false};
    std::atomic<bool> reconnect_task_running{false};
    int retry_count = 0;
    DeepDogMqttClient* owner = nullptr;

    static void OnMqttEvent(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);
    static void OnReconnectTimer(void* arg);
    static void ReconnectTask(void* arg);
    void ScheduleReconnect();
    bool DoConnect();
    void DestroyHandleUnlocked();
};

DeepDogMqttClient::DeepDogMqttClient() : impl_(std::make_unique<Impl>()) {
    impl_->owner = this;
    impl_->events = xEventGroupCreate();
    impl_->mutex = xSemaphoreCreateMutex();
    esp_timer_create_args_t targs = {
        .callback = &Impl::OnReconnectTimer,
        .arg = impl_.get(),
        .dispatch_method = ESP_TIMER_TASK,
        .name = "dog_mqtt_reconn",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&targs, &impl_->reconnect_timer);
}

DeepDogMqttClient::~DeepDogMqttClient() {
    Stop();
    if (impl_->reconnect_timer) {
        esp_timer_stop(impl_->reconnect_timer);
        esp_timer_delete(impl_->reconnect_timer);
        impl_->reconnect_timer = nullptr;
    }
    if (impl_->events) {
        vEventGroupDelete(impl_->events);
        impl_->events = nullptr;
    }
    if (impl_->mutex) {
        vSemaphoreDelete(impl_->mutex);
        impl_->mutex = nullptr;
    }
}

std::string DeepDogMqttClient::Topic(const std::string& relative) const {
    if (relative.empty()) {
        return prefix_;
    }
    if (relative.front() == '/') {
        return prefix_ + relative.substr(1);
    }
    return prefix_ + relative;
}

bool DeepDogMqttClient::IsConnected() const {
    return impl_ && impl_->connected.load(std::memory_order_acquire);
}

void DeepDogMqttClient::Impl::DestroyHandleUnlocked() {
    if (!handle) {
        return;
    }
    esp_mqtt_client_handle_t h = handle;
    handle = nullptr;
    // stop 在 run==true 时会等到 mqtt 任务退出；run==false 时仅告警，仍须 destroy
    esp_mqtt_client_stop(h);
    esp_mqtt_client_destroy(h);
}

bool DeepDogMqttClient::Impl::DoConnect() {
    if (stopping.load(std::memory_order_acquire) || !mutex) {
        return false;
    }
    if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    if (stopping.load(std::memory_order_acquire)) {
        xSemaphoreGive(mutex);
        return false;
    }

    connect_in_progress.store(true, std::memory_order_release);
    DestroyHandleUnlocked();
    connected.store(false, std::memory_order_release);

    const auto& s = owner->settings_;
    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.hostname = s.broker_host.c_str();
    cfg.broker.address.port = s.broker_port;
    cfg.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
    cfg.credentials.client_id = s.client_id.c_str();
    if (!s.username.empty()) {
        cfg.credentials.username = s.username.c_str();
    }
    if (!s.password.empty()) {
        cfg.credentials.authentication.password = s.password.c_str();
    }
    cfg.session.keepalive = s.keepalive_s;
    cfg.network.disable_auto_reconnect = true;
    cfg.network.timeout_ms = kNetworkTimeoutMs;
    cfg.task.stack_size = 6144;
    cfg.buffer.size = kMqttBufferSize;
    cfg.buffer.out_size = kMqttBufferSize;

    handle = esp_mqtt_client_init(&cfg);
    if (!handle) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        connect_in_progress.store(false, std::memory_order_release);
        xSemaphoreGive(mutex);
        return false;
    }
    esp_mqtt_client_register_event(handle, MQTT_EVENT_ANY, &Impl::OnMqttEvent, this);
    xEventGroupClearBits(events, kConnectedBit | kErrorBit);
    if (esp_mqtt_client_start(handle) != ESP_OK) {
        ESP_LOGE(TAG, "esp_mqtt_client_start failed");
        DestroyHandleUnlocked();
        connect_in_progress.store(false, std::memory_order_release);
        xSemaphoreGive(mutex);
        return false;
    }

    EventBits_t bits = xEventGroupWaitBits(events, kConnectedBit | kErrorBit, pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(kConnectTimeoutMs));
    bool ok = (bits & kConnectedBit) != 0;
    if (!ok) {
        ESP_LOGW(TAG, "connect wait failed (bits=0x%lx)", (unsigned long)bits);
        if (reconnect_timer) {
            esp_timer_stop(reconnect_timer);
        }
        DestroyHandleUnlocked();
        connected.store(false, std::memory_order_release);
    }

    connect_in_progress.store(false, std::memory_order_release);
    xSemaphoreGive(mutex);
    return ok;
}

void DeepDogMqttClient::Impl::ScheduleReconnect() {
    if (stopping.load(std::memory_order_acquire) || !reconnect_timer) {
        return;
    }
    if (connect_in_progress.load(std::memory_order_acquire)) {
        return;
    }
    if (kMaxReconnect > 0 && retry_count >= kMaxReconnect) {
        ESP_LOGW(TAG, "reconnect gave up after %d tries", retry_count);
        return;
    }
    retry_count++;
    ESP_LOGI(TAG, "schedule reconnect in %d s (try %d)", kReconnectIntervalUs / 1000000, retry_count);
    esp_timer_stop(reconnect_timer);
    esp_timer_start_once(reconnect_timer, kReconnectIntervalUs);
}

void DeepDogMqttClient::Impl::ReconnectTask(void* arg) {
    auto* self = static_cast<Impl*>(arg);
    if (!self || !self->owner) {
        if (self) {
            self->reconnect_task_running.store(false, std::memory_order_release);
        }
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG, "reconnecting...");
    if (self->DoConnect()) {
        self->retry_count = 0;
        if (self->owner->connection_cb_) {
            self->owner->connection_cb_(true);
        }
    } else if (!self->stopping.load(std::memory_order_acquire)) {
        self->ScheduleReconnect();
    }

    self->reconnect_task_running.store(false, std::memory_order_release);
    vTaskDelete(nullptr);
}

void DeepDogMqttClient::Impl::OnReconnectTimer(void* arg) {
    auto* self = static_cast<Impl*>(arg);
    if (!self || !self->owner || self->stopping.load(std::memory_order_acquire)) {
        return;
    }
    if (self->connect_in_progress.load(std::memory_order_acquire)) {
        // 连接仍在进行：稍后再试，避免与 DoConnect 重叠
        self->ScheduleReconnect();
        return;
    }
    bool expected = false;
    if (!self->reconnect_task_running.compare_exchange_strong(expected, true)) {
        return;
    }
    BaseType_t ok = xTaskCreate(&Impl::ReconnectTask, "dog_mqtt_re", kReconnectTaskStack, self,
                                kReconnectTaskPrio, nullptr);
    if (ok != pdPASS) {
        self->reconnect_task_running.store(false, std::memory_order_release);
        ESP_LOGE(TAG, "reconnect task create failed");
        self->ScheduleReconnect();
    }
}

void DeepDogMqttClient::Impl::OnMqttEvent(void* handler_args, esp_event_base_t base, int32_t event_id,
                                          void* event_data) {
    (void)base;
    auto* self = static_cast<Impl*>(handler_args);
    auto* event = static_cast<esp_mqtt_event_handle_t>(event_data);
    if (!self || !self->owner) {
        return;
    }
    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            // 勿在此回调内 Publish/Subscribe（与 mqtt 任务同栈，易死锁）；由 DoConnect 返回后通知
            self->connected.store(true, std::memory_order_release);
            self->retry_count = 0;
            xEventGroupSetBits(self->events, kConnectedBit);
            ESP_LOGI(TAG, "connected to %s:%d", self->owner->settings_.broker_host.c_str(),
                     self->owner->settings_.broker_port);
            break;
        case MQTT_EVENT_DISCONNECTED:
            if (self->connected.exchange(false)) {
                ESP_LOGW(TAG, "disconnected");
                if (self->owner->connection_cb_) {
                    self->owner->connection_cb_(false);
                }
            }
            xEventGroupSetBits(self->events, kErrorBit);
            // DoConnect 等待期间由 WaitBits 收口，禁止再投递重连（避免 stop/destroy 竞态）
            if (!self->stopping.load(std::memory_order_acquire) &&
                !self->connect_in_progress.load(std::memory_order_acquire)) {
                self->ScheduleReconnect();
            }
            break;
        case MQTT_EVENT_DATA: {
            if (!self->owner->message_cb_ || !event) {
                break;
            }
            std::string topic(event->topic, event->topic_len);
            std::string payload(event->data, event->data_len);
            self->owner->message_cb_(topic, payload);
            break;
        }
        case MQTT_EVENT_ERROR:
            ESP_LOGW(TAG, "mqtt error");
            xEventGroupSetBits(self->events, kErrorBit);
            if (!self->stopping.load(std::memory_order_acquire) &&
                !self->connect_in_progress.load(std::memory_order_acquire)) {
                self->ScheduleReconnect();
            }
            break;
        default:
            break;
    }
}

bool DeepDogMqttClient::Start(const DeepDogMqttSettings& settings) {
    settings_ = settings;
    prefix_ = DeepDogMqttConfig::TopicPrefix(settings_.device_id);
    impl_->stopping.store(false, std::memory_order_release);
    impl_->retry_count = 0;

    ESP_LOGI(TAG, "start prefix=%s", prefix_.c_str());
    if (!impl_->DoConnect()) {
        ESP_LOGW(TAG, "initial connect failed; will retry");
        impl_->ScheduleReconnect();
        return false;
    }
    if (connection_cb_) {
        connection_cb_(true);
    }
    return true;
}

void DeepDogMqttClient::Stop() {
    if (!impl_) {
        return;
    }
    impl_->stopping.store(true, std::memory_order_release);
    if (impl_->reconnect_timer) {
        esp_timer_stop(impl_->reconnect_timer);
    }
    if (impl_->mutex && xSemaphoreTake(impl_->mutex, portMAX_DELAY) == pdTRUE) {
        impl_->DestroyHandleUnlocked();
        xSemaphoreGive(impl_->mutex);
    }
    impl_->connected.store(false, std::memory_order_release);
}

bool DeepDogMqttClient::Publish(const std::string& relative_topic, const std::string& payload, int qos,
                                bool retain) {
    if (!IsConnected() || !impl_->handle) {
        return false;
    }
    const std::string topic = Topic(relative_topic);
    const int msg_id =
        esp_mqtt_client_publish(impl_->handle, topic.c_str(), payload.data(), static_cast<int>(payload.size()),
                                qos, retain ? 1 : 0);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "publish fail %s", topic.c_str());
        return false;
    }
    ESP_LOGD(TAG, "pub %s qos=%d retain=%d len=%u", topic.c_str(), qos, retain ? 1 : 0,
             (unsigned)payload.size());
    return true;
}

bool DeepDogMqttClient::Subscribe(const std::string& relative_topic, int qos) {
    if (!IsConnected() || !impl_->handle) {
        return false;
    }
    const std::string topic = Topic(relative_topic);
    const int msg_id = esp_mqtt_client_subscribe_single(impl_->handle, topic.c_str(), qos);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "subscribe fail %s", topic.c_str());
        return false;
    }
    ESP_LOGI(TAG, "sub %s qos=%d", topic.c_str(), qos);
    return true;
}

bool DeepDogMqttClient::Unsubscribe(const std::string& relative_topic) {
    if (!IsConnected() || !impl_->handle) {
        return false;
    }
    const std::string topic = Topic(relative_topic);
    return esp_mqtt_client_unsubscribe(impl_->handle, topic.c_str()) >= 0;
}
