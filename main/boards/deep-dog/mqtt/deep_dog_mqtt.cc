#include "mqtt/deep_dog_mqtt.h"

#include "mqtt/mqtt_client.h"
#include "mqtt/mqtt_config.h"
#include "mqtt/modules/device_mqtt.h"
#include "mqtt/modules/stream_mqtt.h"
#include "mqtt/modules/imu_mqtt.h"
#include "mqtt/modules/face_mqtt.h"
#include "mqtt/modules/track_mqtt.h"
#include "mqtt/modules/touch_mqtt.h"
#include "mqtt/modules/led_mqtt.h"
#include "touch_btn/touch_event_hub.h"
#include "led/led_strip_control.h"

#include "config.h"
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
    DeepDogTouchMqtt touch{&client};
    DeepDogLedMqtt led{&client};
    VisionFrameHub* hub = nullptr;
    DeepDogHttpServer* http = nullptr;
    TouchEventHub* touch_hub = nullptr;
    TouchButtonController* touch_ctrl = nullptr;
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
        touch.OnConnected();
        led.OnConnected();
    } else {
        device.OnDisconnected();
        stream.OnDisconnected();
        imu.OnDisconnected();
        face.OnDisconnected();
        track.OnDisconnected();
        touch.OnDisconnected();
        led.OnDisconnected();
    }
}

void DeepDogMqtt::Impl::OnMessage(const std::string& topic, const std::string& payload) {
    stream.OnMessage(topic, payload);
    face.OnMessage(topic, payload);
    track.OnMessage(topic, payload);
    led.OnMessage(topic, payload);
}

DeepDogMqtt::DeepDogMqtt() : impl_(std::make_unique<Impl>()) {}

DeepDogMqtt::~DeepDogMqtt() {
    if (impl_ && impl_->touch_hub) {
        impl_->touch_hub->SetPushListener(nullptr);
    }
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

void DeepDogMqtt::SetTouchHub(TouchEventHub* hub) {
    if (!impl_) {
        return;
    }
    if (impl_->touch_hub && impl_->touch_hub != hub) {
        impl_->touch_hub->SetPushListener(nullptr);
    }
    impl_->touch_hub = hub;
    impl_->touch.SetHub(hub);
    if (hub) {
        hub->SetPushListener([this](const TouchEvent& ev) {
            if (impl_) {
                impl_->touch.OnButtonEvent(ev);
            }
        });
    }
}

void DeepDogMqtt::SetTouchController(TouchButtonController* ctrl) {
    if (impl_) {
        impl_->touch_ctrl = ctrl;
        impl_->touch.SetController(ctrl);
    }
}

void DeepDogMqtt::SetTouchComboRecognizer(TouchComboRecognizer* combo) {
    if (impl_) {
        impl_->touch.SetComboRecognizer(combo);
    }
}

void DeepDogMqtt::NotifyTouchCombo(const char* combo_id) {
    if (impl_) {
        impl_->touch.OnComboRecognized(combo_id);
    }
}

void DeepDogMqtt::SetLedControl(LedStripControl* ctrl) {
    if (impl_) {
        impl_->led.SetControl(ctrl);
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
    caps.touch = true;
#if DEEP_DOG_DOG_ENABLE
    caps.dog = true;
#else
    caps.dog = false;
#endif
#if DEEP_DOG_MOTOR_ENABLE
    caps.motor = true;
#else
    caps.motor = false;
#endif
#if DEEP_DOG_CAN_ENABLE
    caps.can = true;
#else
    caps.can = false;
#endif
#if DEEP_DOG_ARM_ENABLE
    caps.arm = true;
#else
    caps.arm = false;
#endif
#if DEEP_DOG_UART_ENABLE
    caps.uart = true;
#else
    caps.uart = false;
#endif
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
#if DEEP_DOG_LED_ENABLE
    caps.led = true;
#else
    caps.led = false;
#endif
#if DEEP_DOG_SERVO_ENABLE
    caps.servo = true;
#else
    caps.servo = false;
#endif
#if DEEP_DOG_GIMBAL_ENABLE
    caps.gimbal = true;
#else
    caps.gimbal = false;
#endif
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
    impl_->touch.SetEnabled(caps.touch);
    impl_->touch.SetHub(impl_->touch_hub);
    impl_->touch.SetController(impl_->touch_ctrl);
    impl_->led.SetEnabled(caps.led);

    impl_->client.SetConnectionCallback([this](bool c) { impl_->OnConnection(c); });
    impl_->client.SetMessageCallback(
        [this](const std::string& t, const std::string& p) { impl_->OnMessage(t, p); });

    const DeepDogMqttSettings settings = DeepDogMqttConfig::Load();
    const bool ok = impl_->client.Start(settings);
    impl_->started = true;
    ESP_LOGI(TAG, "board MQTT started (connected=%d) stream=%d face=%d track=%d imu=%d led=%d", ok ? 1 : 0,
             caps.stream ? 1 : 0, caps.face ? 1 : 0, caps.track ? 1 : 0, caps.imu ? 1 : 0, caps.led ? 1 : 0);
    return ok;
}

void DeepDogMqtt::Stop() {
    if (!impl_ || !impl_->started) {
        return;
    }
    impl_->led.Stop();
    impl_->touch.Stop();
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
void DeepDogMqtt::SetTouchHub(TouchEventHub*) {}
void DeepDogMqtt::SetTouchController(TouchButtonController*) {}
void DeepDogMqtt::SetTouchComboRecognizer(TouchComboRecognizer*) {}
void DeepDogMqtt::NotifyTouchCombo(const char*) {}
void DeepDogMqtt::SetLedControl(LedStripControl*) {}
bool DeepDogMqtt::Start() { return false; }
void DeepDogMqtt::Stop() {}
bool DeepDogMqtt::IsRunning() const { return false; }

#endif
