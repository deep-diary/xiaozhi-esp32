#include "mqtt/deep_dog_mqtt.h"

#include "mqtt/mqtt_client.h"
#include "mqtt/modules/device_mqtt.h"
#include "mqtt/modules/stream_mqtt.h"

#include "face_ai_config.h"
#include "http-server/http_server_config.h"
#include "vision/vision_config.h"

#include <esp_log.h>

#define TAG "dog_mqtt"

#if DEEP_DOG_MQTT_ENABLE

struct DeepDogMqtt::Impl {
    DeepDogMqttClient client;
    DeepDogDeviceMqtt device{&client};
    DeepDogStreamMqtt stream{&client};
    VisionFrameHub* hub = nullptr;
    DeepDogHttpServer* http = nullptr;
    int http_port = DEEP_DOG_HTTP_SERVER_PORT;
    bool started = false;

    void OnConnection(bool connected);
    void OnMessage(const std::string& topic, const std::string& payload);
};

void DeepDogMqtt::Impl::OnConnection(bool connected) {
    if (connected) {
        device.OnConnected();
        stream.OnConnected();
    } else {
        device.OnDisconnected();
        stream.OnDisconnected();
    }
}

void DeepDogMqtt::Impl::OnMessage(const std::string& topic, const std::string& payload) {
    stream.OnMessage(topic, payload);
}

DeepDogMqtt::DeepDogMqtt() : impl_(std::make_unique<Impl>()) {}

DeepDogMqtt::~DeepDogMqtt() {
    Stop();
}

void DeepDogMqtt::SetVisionHub(VisionFrameHub* hub) {
    if (impl_) {
        impl_->hub = hub;
        impl_->stream.SetVisionHub(hub);
    }
}

void DeepDogMqtt::SetHttpServer(DeepDogHttpServer* http) {
    if (impl_) {
        impl_->http = http;
        impl_->stream.SetHttpServer(http);
    }
}

void DeepDogMqtt::SetHttpPort(int port) {
    if (impl_) {
        impl_->http_port = port;
        impl_->device.SetHttpPort(port);
    }
}

bool DeepDogMqtt::IsRunning() const {
    return impl_ && impl_->started;
}

bool DeepDogMqtt::Start() {
    if (!impl_ || impl_->started) {
        return impl_ && impl_->started;
    }

    DeepDogCapabilities caps;
    caps.dog = true;
    caps.touch = true;
    caps.can = true;
#if DEEP_DOG_VISION_HUB_ENABLE
    caps.stream = true;
#else
    caps.stream = false;
#endif
#if DEEP_DOG_FACE_AI_ENABLE
    caps.face = true;
#else
    caps.face = false;
#endif
    caps.imu = false;
    caps.led = false;
    caps.servo = false;
    caps.gimbal = false;
    caps.handle = false;

    impl_->device.SetCapabilities(caps);
    impl_->device.SetHttpPort(impl_->http_port);
    impl_->stream.SetEnabled(caps.stream);
    impl_->stream.SetVisionHub(impl_->hub);
    impl_->stream.SetHttpServer(impl_->http);

    impl_->client.SetConnectionCallback([this](bool c) { impl_->OnConnection(c); });
    impl_->client.SetMessageCallback(
        [this](const std::string& t, const std::string& p) { impl_->OnMessage(t, p); });

    const DeepDogMqttSettings settings = DeepDogMqttConfig::Load();
    const bool ok = impl_->client.Start(settings);
    impl_->started = true;  // 即使首连失败也会后台重连
    ESP_LOGI(TAG, "board MQTT started (connected=%d) stream_cap=%d", ok ? 1 : 0, caps.stream ? 1 : 0);
    return ok;
}

void DeepDogMqtt::Stop() {
    if (!impl_ || !impl_->started) {
        return;
    }
    impl_->stream.Stop();
    impl_->device.Stop();
    impl_->client.Stop();
    impl_->started = false;
}

#else  // !DEEP_DOG_MQTT_ENABLE

DeepDogMqtt::DeepDogMqtt() = default;
DeepDogMqtt::~DeepDogMqtt() = default;
void DeepDogMqtt::SetVisionHub(VisionFrameHub*) {}
void DeepDogMqtt::SetHttpServer(DeepDogHttpServer*) {}
void DeepDogMqtt::SetHttpPort(int) {}
bool DeepDogMqtt::Start() { return false; }
void DeepDogMqtt::Stop() {}
bool DeepDogMqtt::IsRunning() const { return false; }

#endif
