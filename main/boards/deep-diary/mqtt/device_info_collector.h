#ifndef DEVICE_INFO_COLLECTOR_H
#define DEVICE_INFO_COLLECTOR_H

#include "user_mqtt_client.h"
#include "../motor/deep_motor.h"
#include "../arm/deep_arm.h"
#include "../gimbal/Gimbal.h"
#include "../../led/circular_strip.h"
#include "../led/led_control.h"
#include "../../common/esp32_camera.h"
#include "../sensor/QMA6100P/qma6100p.h"
#include <string>
#include <memory>

class DeviceInfoCollector {
public:
    DeviceInfoCollector();
    ~DeviceInfoCollector();
    
    // 设置设备组件引用
    void SetDeepMotor(DeepMotor* motor);
    void SetDeepArm(DeepArm* arm);
    void SetGimbal(Gimbal_t* gimbal);
    void SetLedStrip(CircularStrip* led_strip);
    void SetLedControl(LedStripControl* led_control);
    void SetCamera(Esp32Camera* camera);
    void SetSensor(qma6100p_rawdata_t* sensor_data);
    
    // 收集设备固定配置信息
    DeviceConfigInfo CollectDeviceConfig();
    
    // 收集各种状态信息
    DeviceStatus::SystemInfo CollectSystemStatus();
    DeviceStatus::SensorData CollectSensorStatus();
    DeviceStatus::ActuatorStatus CollectActuatorStatus();
    
    // 收集 Thumbler 完整状态
    DeviceStatus::ThumblerStatus CollectThumblerStatus();
    
    // 获取特定组件状态
    std::string GetArmStatus() const;
    std::string GetMotorStatus() const;
    std::string GetGimbalStatus() const;
    std::string GetLedStripStatus() const;
    std::string GetCameraStatus() const;
    std::string GetSensorStatus() const;
    
    // 获取系统信息
    std::string GetDeviceId() const;
    std::string GetFirmwareVersion() const;
    std::string GetWifiInfo() const;
    std::string GetIpAddress() const;
    int GetFreeHeap() const;
    std::string GetDetailedMemoryInfo() const;
    int GetUptimeSeconds() const;
    float GetCpuTemperature() const;
    std::string GetMacAddress() const;
    std::string GetChipModel() const;
    std::string GetChipRevision() const;
    
private:
    DeepMotor* deep_motor_;
    DeepArm* deep_arm_;
    Gimbal_t* gimbal_;
    CircularStrip* led_strip_;
    LedStripControl* led_control_;  // LED控制类（用于获取状态）
    Esp32Camera* camera_;
    qma6100p_rawdata_t* sensor_data_;
    
    // 缓存设备ID
    mutable std::string cached_device_id_;
    mutable bool device_id_cached_;
    
    // 内部方法
    std::string GenerateDeviceId() const;
    std::string GetChipFeatures() const;
    std::string GetFlashInfo() const;
    std::string GetPsramInfo() const;
};

#endif // DEVICE_INFO_COLLECTOR_H

