#include "device_info_collector.h"
#include "../config.h"
#include "wifi_station.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <sstream>
#include <iomanip>

#if ENABLE_TCP_CLIENT_MODE
#include "../streaming/tcp_client.h"
#endif

#define TAG_DEVICE_INFO "DeviceInfo"

DeviceInfoCollector::DeviceInfoCollector() 
    : deep_motor_(nullptr), deep_arm_(nullptr), gimbal_(nullptr), 
      led_strip_(nullptr), led_control_(nullptr), camera_(nullptr), sensor_data_(nullptr), device_id_cached_(false) {
    ESP_LOGI(TAG_DEVICE_INFO, "DeviceInfoCollector initialized");
}

DeviceInfoCollector::~DeviceInfoCollector() {
    ESP_LOGI(TAG_DEVICE_INFO, "DeviceInfoCollector destroyed");
}

void DeviceInfoCollector::SetDeepMotor(DeepMotor* motor) {
    deep_motor_ = motor;
}

void DeviceInfoCollector::SetDeepArm(DeepArm* arm) {
    deep_arm_ = arm;
}

void DeviceInfoCollector::SetGimbal(Gimbal_t* gimbal) {
    gimbal_ = gimbal;
}

void DeviceInfoCollector::SetLedStrip(CircularStrip* led_strip) {
    led_strip_ = led_strip;
}

void DeviceInfoCollector::SetLedControl(LedStripControl* led_control) {
    led_control_ = led_control;
}

void DeviceInfoCollector::SetCamera(Esp32Camera* camera) {
    camera_ = camera;
}

void DeviceInfoCollector::SetSensor(qma6100p_rawdata_t* sensor_data) {
    sensor_data_ = sensor_data;
}

std::string DeviceInfoCollector::GetArmStatus() const {
    if (!deep_arm_) {
        return "not_available";
    }
    
    // 这里可以根据实际的DeepArm API来获取状态
    // 暂时返回基本状态
    return "connected";
}

std::string DeviceInfoCollector::GetMotorStatus() const {
    if (!deep_motor_) {
        return "not_available";
    }
    
    // 这里可以根据实际的DeepMotor API来获取状态
    // 暂时返回基本状态
    return "connected";
}

std::string DeviceInfoCollector::GetGimbalStatus() const {
    if (!gimbal_) {
        return "not_available";
    }
    
    // 这里可以根据实际的Gimbal API来获取状态
    // 暂时返回基本状态
    return "connected";
}

std::string DeviceInfoCollector::GetLedStripStatus() const {
    if (!led_strip_) {
        return "not_available";
    }
    
    // 这里可以根据实际的CircularStrip API来获取状态
    // 暂时返回基本状态
    return "connected";
}

std::string DeviceInfoCollector::GetCameraStatus() const {
    if (!camera_) {
        return "not_available";
    }
    
    // 这里可以根据实际的Esp32Camera API来获取状态
    // 暂时返回基本状态
    return "connected";
}

std::string DeviceInfoCollector::GetSensorStatus() const {
    if (!sensor_data_) {
        ESP_LOGI(TAG_DEVICE_INFO, "Sensor data is not available");
        return "not_available";
    }
    
    // 检查传感器数据是否有效
    if (sensor_data_->acc_g > 0.1f) { // 如果总加速度大于0.1，认为传感器工作正常
        return "connected";
    } else {
        return "error";
    }
}

std::string DeviceInfoCollector::GetDeviceId() const {
    if (!device_id_cached_) {
        cached_device_id_ = GenerateDeviceId();
        device_id_cached_ = true;
    }
    return cached_device_id_;
}

std::string DeviceInfoCollector::GetFirmwareVersion() const {
    // 这里可以从版本文件或编译时定义中获取
    return "1.0.0";
}

std::string DeviceInfoCollector::GetWifiInfo() const {
    auto& wifi_station = WifiStation::GetInstance();
    return wifi_station.GetSsid();
}

std::string DeviceInfoCollector::GetIpAddress() const {
    // 获取IP地址，这里需要根据实际的WifiStation API来获取
    // 暂时返回空字符串，实际使用时需要实现正确的IP获取方法
    // TODO: 实现正确的IP地址获取
    return "";
}

int DeviceInfoCollector::GetFreeHeap() const {
    return esp_get_free_heap_size();
}

std::string DeviceInfoCollector::GetDetailedMemoryInfo() const {
    std::stringstream ss;
    
    // 基本堆内存信息
    size_t free_heap = esp_get_free_heap_size();
    size_t min_free_heap = esp_get_minimum_free_heap_size();
    
    // 不同类型内存信息
    size_t free_internal_ram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t free_dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
    
    // 总内存信息
    size_t total_internal_ram = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t total_spiram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    
    ss << "Free: " << (free_heap / 1024) << "KB";
    ss << " | Min: " << (min_free_heap / 1024) << "KB";
    ss << " | Internal: " << (free_internal_ram / 1024) << "/" << (total_internal_ram / 1024) << "KB";
    if (total_spiram > 0) {
        ss << " | PSRAM: " << (free_spiram / 1024) << "/" << (total_spiram / 1024) << "KB";
    }
    ss << " | DMA: " << (free_dma / 1024) << "KB";
    
    return ss.str();
}

int DeviceInfoCollector::GetUptimeSeconds() const {
    return esp_timer_get_time() / 1000000; // 转换为秒
}

float DeviceInfoCollector::GetCpuTemperature() const {
    // ESP32-S3没有内置温度传感器，返回0
    return 0.0f;
}

std::string DeviceInfoCollector::GenerateDeviceId() const {
    std::string mac = GetMacAddress();
    std::string chip_model = GetChipModel();
    
    // 使用MAC地址和芯片型号生成设备ID
    std::stringstream ss;
    ss << "ATK-DNESP32S3-" << chip_model << "-" << mac.substr(0, 8);
    return ss.str();
}

std::string DeviceInfoCollector::GetMacAddress() const {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    
    std::stringstream ss;
    for (int i = 0; i < 6; i++) {
        ss << std::hex << std::setfill('0') << std::setw(2) << (int)mac[i];
        if (i < 5) ss << ":";
    }
    return ss.str();
}

std::string DeviceInfoCollector::GetChipModel() const {
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    switch (chip_info.model) {
        case CHIP_ESP32: return "ESP32";
        case CHIP_ESP32S2: return "ESP32-S2";
        case CHIP_ESP32S3: return "ESP32-S3";
        case CHIP_ESP32C3: return "ESP32-C3";
        case CHIP_ESP32C6: return "ESP32-C6";
        case CHIP_ESP32H2: return "ESP32-H2";
        default: return "Unknown";
    }
}

std::string DeviceInfoCollector::GetChipRevision() const {
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    std::stringstream ss;
    ss << chip_info.revision;
    return ss.str();
}

std::string DeviceInfoCollector::GetChipFeatures() const {
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    std::stringstream ss;
    if (chip_info.features & CHIP_FEATURE_WIFI_BGN) ss << "WiFi ";
    if (chip_info.features & CHIP_FEATURE_BT) ss << "BT ";
    if (chip_info.features & CHIP_FEATURE_BLE) ss << "BLE ";
    if (chip_info.features & CHIP_FEATURE_IEEE802154) ss << "802.15.4 ";
    if (chip_info.features & CHIP_FEATURE_EMB_FLASH) ss << "Embedded-Flash ";
    // CHIP_FEATURE_EXTERNAL_FLASH 在某些ESP-IDF版本中可能不可用
    // if (chip_info.features & CHIP_FEATURE_EXTERNAL_FLASH) ss << "External-Flash ";
    
    return ss.str();
}

std::string DeviceInfoCollector::GetFlashInfo() const {
    uint32_t flash_size;
    esp_flash_get_size(nullptr, &flash_size);
    
    std::stringstream ss;
    ss << flash_size / (1024 * 1024) << "MB";
    return ss.str();
}

std::string DeviceInfoCollector::GetPsramInfo() const {
    std::stringstream ss;
    
    // 检查PSRAM是否可用 - 使用更兼容的方法
    #ifdef CONFIG_SPIRAM
    // 如果配置了SPIRAM，尝试获取大小
    extern size_t esp_spiram_get_size(void);
    size_t psram_size = esp_spiram_get_size();
    if (psram_size > 0) {
        ss << psram_size / (1024 * 1024) << "MB";
    } else {
        ss << "Available but size unknown";
    }
    #else
    ss << "Not available";
    #endif
    
    return ss.str();
}

DeviceConfigInfo DeviceInfoCollector::CollectDeviceConfig() {
    DeviceConfigInfo config;
    
    config.device_id = GetDeviceId();
    config.device_type = "ATK-DNESP32S3";
    config.firmware_version = GetFirmwareVersion();
    config.mac_address = GetMacAddress();
    config.chip_model = GetChipModel();
    config.chip_revision = GetChipRevision();
    
    // 硬件能力
    config.capabilities.camera = (camera_ != nullptr);
    config.capabilities.can_bus = true; // CAN总线在板级初始化中已配置
    config.capabilities.led_strip = (led_strip_ != nullptr);
    config.capabilities.gimbal = (gimbal_ != nullptr);
    config.capabilities.arm = (deep_arm_ != nullptr);
    config.capabilities.motor = (deep_motor_ != nullptr);
    config.capabilities.sensor = (sensor_data_ != nullptr);
    
    ESP_LOGI(TAG_DEVICE_INFO, "Device config collected - ID: %s", config.device_id.c_str());
    
    return config;
}

DeviceStatus::SystemInfo DeviceInfoCollector::CollectSystemStatus() {
    DeviceStatus::SystemInfo status;
    
    status.wifi_ssid = GetWifiInfo();
    status.ip_address = GetIpAddress();
    status.free_heap = GetFreeHeap();
    status.uptime_seconds = GetUptimeSeconds();
    status.cpu_temperature = GetCpuTemperature();
    status.network_status = "connected"; // 简化处理
    
    return status;
}

DeviceStatus::SensorData DeviceInfoCollector::CollectSensorStatus() {
    DeviceStatus::SensorData sensor_data;
    
    // 如果设置了传感器数据指针，直接使用（兼容外部传入数据的情况）
    if (sensor_data_ != nullptr) {
        sensor_data.acc_x = sensor_data_->acc_x;
        sensor_data.acc_y = sensor_data_->acc_y;
        sensor_data.acc_z = sensor_data_->acc_z;
        sensor_data.acc_g = sensor_data_->acc_g;
        sensor_data.pitch = sensor_data_->pitch;
        sensor_data.roll = sensor_data_->roll;
        sensor_data.sensor_status = GetSensorStatus();
    } else {
        // 指针未设置，直接读取传感器（实时读取最新数据）
        // 注意：传感器驱动需要在初始化时调用 qma6100p_init()
        qma6100p_rawdata_t raw_data;
        qma6100p_read_rawdata(&raw_data);
        
        sensor_data.acc_x = raw_data.acc_x;
        sensor_data.acc_y = raw_data.acc_y;
        sensor_data.acc_z = raw_data.acc_z;
        sensor_data.acc_g = raw_data.acc_g;
        sensor_data.pitch = raw_data.pitch;
        sensor_data.roll = raw_data.roll;
        
        // 根据读取的数据判断传感器状态
        if (raw_data.acc_g > 0.1f) {
            sensor_data.sensor_status = "connected";
        } else {
            sensor_data.sensor_status = "error";
        }
    }
    
    return sensor_data;
}

DeviceStatus::ActuatorStatus DeviceInfoCollector::CollectActuatorStatus() {
    DeviceStatus::ActuatorStatus actuator_status;
    
    // 机械臂状态
    actuator_status.arm.connected = (deep_arm_ != nullptr);
    actuator_status.arm.motor_count = actuator_status.arm.connected ? 6 : 0; // 假设6个电机
    actuator_status.arm.status = GetArmStatus();
    
    // 电机状态
    actuator_status.motor.connected = (deep_motor_ != nullptr);
    actuator_status.motor.motor_count = actuator_status.motor.connected ? 6 : 0;
    actuator_status.motor.status = GetMotorStatus();
    
    return actuator_status;
}

DeviceStatus::ThumblerStatus DeviceInfoCollector::CollectThumblerStatus() {
    DeviceStatus::ThumblerStatus status;
    
    // 摄像头状态 - 通过TCP客户端连接状态判断
#if ENABLE_TCP_CLIENT_MODE
    tcp_client_state_t tcp_state = tcp_client_get_state();
    status.cur_cam_switch = (tcp_state == TCP_CLIENT_CONNECTED || tcp_state == TCP_CLIENT_CONNECTING);
#else
    status.cur_cam_switch = false;
#endif
    
    // 传感器数据
    DeviceStatus::SensorData sensor_data = CollectSensorStatus();
    status.g_acc_x = sensor_data.acc_x;
    status.g_acc_y = sensor_data.acc_y;
    status.g_acc_z = sensor_data.acc_z;
    status.g_acc_g = sensor_data.acc_g;
    status.g_pitch = sensor_data.pitch;
    status.g_roll = sensor_data.roll;
    
    // LED 状态 - 从 LED 控制类获取当前状态（优先使用led_control_，因为它跟踪了所有状态变化）
    if (led_control_) {
        status.cur_led_mode = led_control_->GetCurrentMode();
        status.cur_led_brightness = led_control_->GetDefaultBrightness();
        status.cur_led_low_brightness = led_control_->GetLowBrightness();
        StripColor current_color = led_control_->GetCurrentColor();
        status.cur_led_color_red = current_color.red;
        status.cur_led_color_green = current_color.green;
        status.cur_led_color_blue = current_color.blue;
        // 注意：ThumblerStatus中没有low_color字段，如果需要可以添加
        status.cur_led_interval_ms = led_control_->GetCurrentInterval();
        status.cur_led_scroll_length = led_control_->GetCurrentScrollLength();
    } else {
        // LED控制类未初始化，使用默认值
        status.cur_led_mode = 0;
        status.cur_led_brightness = 128;
        status.cur_led_low_brightness = 16;
        status.cur_led_color_red = 0;
        status.cur_led_color_green = 0;
        status.cur_led_color_blue = 0;
        status.cur_led_interval_ms = 500;
        status.cur_led_scroll_length = 3;
    }
    
    // 不倒翁状态
    status.cur_tumbler_mode = 0;  // 默认静止
    // TODO: 从实际的不倒翁控制模块获取当前模式
    
    // 人员检测
    status.is_has_people = false;  // TODO: 从人员检测模块获取
    // 这里可能需要从摄像头或其他传感器获取人员检测结果
    
    // 电量
    // TODO: 从电池管理模块获取实际电量
    status.power_percent = 100;  // 默认100%，实际需要从电池管理模块读取
    
    return status;
}

