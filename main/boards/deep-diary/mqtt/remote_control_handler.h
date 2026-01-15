#ifndef REMOTE_CONTROL_HANDLER_H
#define REMOTE_CONTROL_HANDLER_H

#include "user_mqtt_client.h"
#include "../motor/deep_motor.h"
#include "../arm/deep_arm.h"
#include "../gimbal/Gimbal.h"
#include "../../led/circular_strip.h"
#include "../led/led_control.h"
#include "../../common/esp32_camera.h"
#include <string>
#include <memory>
#include <functional>

class RemoteControlHandler {
public:
    RemoteControlHandler();
    ~RemoteControlHandler();
    
    // 设置设备组件引用
    void SetDeepMotor(DeepMotor* motor);
    void SetDeepArm(DeepArm* arm);
    void SetGimbal(Gimbal_t* gimbal);
    void SetLedStrip(CircularStrip* led_strip);
    void SetLedControl(LedStripControl* led_control);
    void SetCamera(Esp32Camera* camera);
    
    // 处理 Thumbler 控制命令
    void HandleThumblerCommand(const ThumblerControlCommand& command);
    
    // 设置MQTT客户端引用（用于发送事件反馈）
    void SetMqttClient(UserMqttClient* mqtt_client);
    
private:
    DeepMotor* deep_motor_;
    DeepArm* deep_arm_;
    Gimbal_t* gimbal_;
    CircularStrip* led_strip_;
    LedStripControl* led_control_;  // LED控制类（优先使用，确保状态同步）
    Esp32Camera* camera_;
    UserMqttClient* mqtt_client_;
    
    // Thumbler 命令处理函数
    void HandleThumblerLedControl(const ThumblerControlCommand& command);
    void HandleThumblerCameraControl(const ThumblerControlCommand& command);
    void HandleThumblerTumblerControl(const ThumblerControlCommand& command);
    
    // 辅助函数
    void SendEvent(const std::string& event_type, const std::string& message, const cJSON* data = nullptr);
};

#endif // REMOTE_CONTROL_HANDLER_H

