#include "mqtt/deep_dog_mqtt.h"

#include "mqtt/mqtt_client.h"
#include "mqtt/mqtt_config.h"
#include "mqtt/modules/device_mqtt.h"
#include "mqtt/modules/stream_mqtt.h"
#include "mqtt/modules/imu_mqtt.h"
#include "mqtt/modules/face_mqtt.h"
#include "mqtt/modules/track_mqtt.h"

#include "face_ai_config.h"
#include "http-server/http_server_config.h"
#include "sensor/imu_config.h"
#if DEEP_DOG_IMU_ENABLE
#include "sensor/imu_sensor.h"
#include "sensor/imu_switch.h"
#endif
#include "vision/vision_config.h"

#include <esp_log.h>

#define TAG "dog_mqtt"

#if DEEP_DOG_MQTT_ENABLE

struct DeepDogMqtt::Impl {
    DeepDogMqttClient client;
    DeepDogDeviceMqtt device{&client};
    DeepDogStreamMqtt stream{&client};
    DeepDogImuMqtt imu{&client};
    DeepDogFaceMqtt face{&client};
    DeepDogTrackMqtt track{&client};
    VisionFrameHub* hub = nullptr;
    DeepDogHttpServer* http = nullptr;
#if DEEP_DOG_IMU_ENABLE
    DeepDogImuSensor* imu_sensor = nullptr;
    DeepDogImuSwitch* imu_switch = nullptr;
#endif
    int http_port = DEEP_DOG_HTTP_SERVER_PORT;
    bool started = false;

    void OnConnection(bool connected);
    void OnMessage(const std::string& topic, const std::string& payload);
};

void DeepDogMqtt::Impl::OnConnection(bool connected) {
    if (connected) {
        device.OnConnected();
        stream.OnConnected();
        imu.OnConnected();
        face.OnConnected();
        track.OnConnected();
    } else {
        device.OnDisconnected();
        stream.OnDisconnected();
        imu.OnDisconnected();
        face.OnDisconnected();
        track.OnDisconnected();
    }
}

void DeepDogMqtt::Impl::OnMessage(const std::string& topic, const std::string& payload) {
    stream.OnMessage(topic, payload);
    face.OnMessage(topic, payload);
    track.OnMessage(topic, payload);
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

void DeepDogMqtt::SetImuSensor(DeepDogImuSensor* sensor) {
#if DEEP_DOG_IMU_ENABLE
    if (impl_) {
        impl_->imu_sensor = sensor;
        impl_->imu.SetSensor(sensor);
    }
#else
    (void)sensor;
#endif
}

void DeepDogMqtt::SetImuSwitch(DeepDogImuSwitch* hub) {
#if DEEP_DOG_IMU_ENABLE
    if (impl_) {
        impl_->imu_switch = hub;
        impl_->imu.SetSwitchHub(hub);
    }
#else
    (void)hub;
#endif
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
#if DEEP_DOG_TRACK_MQTT_ENABLE
    caps.track = true;
#else
    caps.track = false;
#endif
#if DEEP_DOG_IMU_ENABLE
    caps.imu = true;
#else
    caps.imu = false;
#endif
    caps.led = false;
    caps.servo = false;
    caps.gimbal = false;
    caps.handle = false;

    impl_->device.SetCapabilities(caps);
    impl_->device.SetHttpPort(impl_->http_port);
    impl_->stream.SetEnabled(caps.stream);
    impl_->stream.SetVisionHub(impl_->hub);
    impl_->stream.SetHttpServer(impl_->http);
    impl_->imu.SetEnabled(caps.imu);
#if DEEP_DOG_IMU_ENABLE
    impl_->imu.SetSensor(impl_->imu_sensor);
    impl_->imu.SetSwitchHub(impl_->imu_switch);
#endif
    impl_->face.SetEnabled(caps.face);
    impl_->track.SetModuleEnabled(caps.track);

    impl_->client.SetConnectionCallback([this](bool c) { impl_->OnConnection(c); });
    impl_->client.SetMessageCallback(
        [this](const std::string& t, const std::string& p) { impl_->OnMessage(t, p); });

    const DeepDogMqttSettings settings = DeepDogMqttConfig::Load();
    const bool ok = impl_->client.Start(settings);
    impl_->started = true;
    ESP_LOGI(TAG, "board MQTT started (connected=%d) stream=%d face=%d track=%d imu=%d", ok ? 1 : 0,
             caps.stream ? 1 : 0, caps.face ? 1 : 0, caps.track ? 1 : 0, caps.imu ? 1 : 0);
    return ok;
}

void DeepDogMqtt::Stop() {
    if (!impl_ || !impl_->started) {
        return;
    }
    impl_->track.Stop();
    impl_->face.Stop();
    impl_->imu.Stop();
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
void DeepDogMqtt::SetImuSensor(DeepDogImuSensor*) {}
void DeepDogMqtt::SetImuSwitch(DeepDogImuSwitch*) {}
bool DeepDogMqtt::Start() { return false; }
void DeepDogMqtt::Stop() {}
bool DeepDogMqtt::IsRunning() const { return false; }

#endif
