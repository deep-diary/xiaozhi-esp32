#include "mqtt/deep_dog_mqtt.h"

#include "mqtt/mqtt_client.h"
#include "mqtt/mqtt_config.h"
#include "mqtt/modules/device_mqtt.h"
#include "mqtt/modules/pairing_mqtt.h"
#include "mqtt/modules/stream_mqtt.h"
#include "mqtt/modules/imu_mqtt.h"
#include "mqtt/modules/face_mqtt.h"
#include "mqtt/modules/track_mqtt.h"
#include "mqtt/modules/touch_mqtt.h"
#include "mqtt/modules/handle_mqtt.h"
#include "mqtt/modules/led_mqtt.h"
#include "mqtt/modules/servo_mqtt.h"
#include "mqtt/modules/gimbal_mqtt.h"
#include "mqtt/modules/can_mqtt.h"
#include "mqtt/modules/motor_mqtt.h"
#include "touch_btn/touch_event_hub.h"
#include "handle/handle_event_hub.h"
#include "handle/handle_config.h"
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
#include "vision/vision_frame_hub.h"

#include <esp_log.h>

#define TAG "dog_mqtt"

#if DEEP_DOG_MQTT_ENABLE

struct DeepDogMqtt::Impl {
    DeepDogMqttClient client;
    DeepDogDeviceMqtt device{&client};
    DeepDogPairingMqtt pairing{&client};
    DeepDogStreamMqtt stream{&client};
    DeepDogImuMqtt imu{&client};
    DeepDogFaceMqtt face{&client};
    DeepDogTrackMqtt track{&client};
    DeepDogTouchMqtt touch{&client};
    DeepDogHandleMqtt handle{&client};
    DeepDogLedMqtt led{&client};
    DeepDogServoMqtt servo{&client};
    DeepDogGimbalMqtt gimbal{&client};
    DeepDogCanMqtt can{&client};
    DeepDogMotorMqtt motor{&client};
    VisionFrameHub* hub = nullptr;
    DeepDogHttpServer* http = nullptr;
    TouchEventHub* touch_hub = nullptr;
    TouchButtonController* touch_ctrl = nullptr;
    HandleEventHub* handle_hub = nullptr;
#if DEEP_DOG_IMU_ENABLE
    DeepDogImuSensor* imu_sensor = nullptr;
    DeepDogImuSwitch* imu_switch = nullptr;
#endif
    int http_port = DEEP_DOG_HTTP_SERVER_PORT;
    bool started = false;

    void OnConnection(bool connected);
    void OnMessage(const std::string& topic, const std::string& payload);
    void ApplyVisionStreamUrl(const DeepDogMqttSettings& settings);
    void StopModulesForReload();
};

void DeepDogMqtt::Impl::OnConnection(bool connected) {
    if (connected) {
        device.OnConnected();
        pairing.OnConnected();
        stream.OnConnected();
        imu.OnConnected();
        face.OnConnected();
        track.OnConnected();
        touch.OnConnected();
        handle.OnConnected();
        led.OnConnected();
        servo.OnConnected();
        gimbal.OnConnected();
        can.OnConnected();
        motor.OnConnected();
    } else {
        device.OnDisconnected();
        pairing.OnDisconnected();
        stream.OnDisconnected();
        imu.OnDisconnected();
        face.OnDisconnected();
        track.OnDisconnected();
        touch.OnDisconnected();
        handle.OnDisconnected();
        led.OnDisconnected();
        servo.OnDisconnected();
        gimbal.OnDisconnected();
        can.OnDisconnected();
        motor.OnDisconnected();
    }
}

void DeepDogMqtt::Impl::OnMessage(const std::string& topic, const std::string& payload) {
    pairing.OnMessage(topic, payload);
    stream.OnMessage(topic, payload);
    face.OnMessage(topic, payload);
    track.OnMessage(topic, payload);
    led.OnMessage(topic, payload);
    touch.OnMessage(topic, payload);
    handle.OnMessage(topic, payload);
    servo.OnMessage(topic, payload);
    gimbal.OnMessage(topic, payload);
    can.OnMessage(topic, payload);
    motor.OnMessage(topic, payload);
}

DeepDogMqtt::DeepDogMqtt() : impl_(std::make_unique<Impl>()) {
    impl_->pairing.SetIdentityReloadCallback([this]() { ReloadDeviceIdentity(); });
}

DeepDogMqtt::~DeepDogMqtt() {
    if (impl_ && impl_->touch_hub) {
        impl_->touch_hub->SetPushListener(nullptr);
    }
    if (impl_ && impl_->handle_hub) {
        impl_->handle_hub->SetPushListener(nullptr);
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

void DeepDogMqtt::SetHandleHub(HandleEventHub* hub) {
    if (!impl_) {
        return;
    }
    if (impl_->handle_hub && impl_->handle_hub != hub) {
        impl_->handle_hub->SetPushListener(nullptr);
    }
    impl_->handle_hub = hub;
    impl_->handle.SetHub(hub);
    if (hub) {
        hub->SetPushListener([this](const HandleSnapshot& snap) {
            if (impl_) {
                impl_->handle.OnSnapshot(snap);
            }
        });
    }
}

void DeepDogMqtt::SetDeepMotor(DeepMotor* motor) {
    if (impl_) {
        impl_->motor.SetMotor(motor);
    }
}

void DeepDogMqtt::SetWsMcpEndpoint(int port, const char* path) {
    if (impl_) {
        impl_->device.SetWsMcpEndpoint(port, path);
        if (port > 0 && impl_->client.IsConnected()) {
            impl_->device.PublishInfo();
        }
    }
}

bool DeepDogMqtt::IsRunning() const {
    return impl_ && impl_->started;
}

void DeepDogMqtt::StartPairingSessionOrAnnounceBound() {
    if (impl_) {
        impl_->pairing.StartPairingSessionOrAnnounceBound();
    }
}

void DeepDogMqtt::RequestDeviceUnbind() {
    if (impl_) {
        impl_->pairing.RequestUnbind();
    }
}

bool DeepDogMqtt::IsDeviceBound() const {
    return impl_ && impl_->pairing.IsBound();
}

const std::string& DeepDogMqtt::DevicePairCode() const {
    static const std::string kEmpty;
    return impl_ ? impl_->pairing.pair_code() : kEmpty;
}

void DeepDogMqtt::Impl::ApplyVisionStreamUrl(const DeepDogMqttSettings& settings) {
#if DEEP_DOG_VISION_HUB_ENABLE
    if (hub) {
        hub->SetRtspUrl(DeepDogMqttConfig::RtspPushUrlForDeviceId(settings.device_id));
    }
#else
    (void)settings;
#endif
}

void DeepDogMqtt::Impl::StopModulesForReload() {
    motor.Stop();
    can.Stop();
    gimbal.Stop();
    servo.Stop();
    led.Stop();
    handle.Stop();
    touch.Stop();
    track.Stop();
    face.Stop();
    imu.Stop();
    stream.Stop();
    pairing.Stop();
    device.Stop();
    client.Stop();
}

void DeepDogMqtt::ReloadDeviceIdentity() {
    if (!impl_ || !impl_->started) {
        return;
    }
    const DeepDogMqttSettings settings = DeepDogMqttConfig::Load();
    impl_->ApplyVisionStreamUrl(settings);
    impl_->StopModulesForReload();
    impl_->stream.SetVisionHub(impl_->hub);
    impl_->stream.SetHttpServer(impl_->http);
    const bool ok = impl_->client.Start(settings);
    ESP_LOGI(TAG, "ReloadDeviceIdentity device_id=%s connected=%d", settings.device_id.c_str(), ok ? 1 : 0);
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
#if DEEP_DOG_HANDLE_ENABLE
    caps.handle = true;
#else
    caps.handle = false;
#endif

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
    impl_->handle.SetEnabled(caps.handle);
    impl_->handle.SetHub(impl_->handle_hub);
    impl_->led.SetEnabled(caps.led);
    impl_->servo.SetEnabled(caps.servo);
    impl_->gimbal.SetEnabled(caps.gimbal);
    impl_->can.SetEnabled(caps.can);
    impl_->motor.SetEnabled(caps.motor);

    impl_->client.SetConnectionCallback([this](bool c) { impl_->OnConnection(c); });
    impl_->client.SetMessageCallback(
        [this](const std::string& t, const std::string& p) { impl_->OnMessage(t, p); });

    const DeepDogMqttSettings settings = DeepDogMqttConfig::Load();
    impl_->ApplyVisionStreamUrl(settings);
    const bool ok = impl_->client.Start(settings);
    impl_->started = true;
    ESP_LOGI(TAG,
             "board MQTT started (connected=%d) stream=%d face=%d track=%d imu=%d led=%d servo=%d "
             "gimbal=%d handle=%d can=%d motor=%d",
             ok ? 1 : 0, caps.stream ? 1 : 0, caps.face ? 1 : 0, caps.track ? 1 : 0, caps.imu ? 1 : 0,
             caps.led ? 1 : 0, caps.servo ? 1 : 0, caps.gimbal ? 1 : 0, caps.handle ? 1 : 0,
             caps.can ? 1 : 0, caps.motor ? 1 : 0);
    return ok;
}

void DeepDogMqtt::Stop() {
    if (!impl_ || !impl_->started) {
        return;
    }
    impl_->StopModulesForReload();
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
void DeepDogMqtt::SetHandleHub(HandleEventHub*) {}
void DeepDogMqtt::SetDeepMotor(DeepMotor*) {}
void DeepDogMqtt::SetWsMcpEndpoint(int, const char*) {}
bool DeepDogMqtt::Start() { return false; }
void DeepDogMqtt::Stop() {}
void DeepDogMqtt::ReloadDeviceIdentity() {}
bool DeepDogMqtt::IsRunning() const { return false; }
void DeepDogMqtt::StartPairingSessionOrAnnounceBound() {}
void DeepDogMqtt::RequestDeviceUnbind() {}
bool DeepDogMqtt::IsDeviceBound() const { return false; }
const std::string& DeepDogMqtt::DevicePairCode() const {
    static const std::string kEmpty;
    return kEmpty;
}

#endif
