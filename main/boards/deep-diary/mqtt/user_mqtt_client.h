#ifndef USER_MQTT_CLIENT_H
#define USER_MQTT_CLIENT_H

#include <string>
#include <memory>
#include <functional>
#include <cJSON.h>
#include <mqtt.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include "user_mqtt_config.h"

#define TAG_USER_MQTT "UserMQTT"

// 用户MQTT配置结构体
struct UserMqttClientConfig {
    std::string broker_host;
    int broker_port = 1883;
    std::string client_id;
    std::string username;
    std::string password;
    std::string device_info_topic;    // 设备信息发布主题（静态配置）
    std::string control_topic;        // 远程控制订阅主题
    std::string status_topic;         // 状态发布主题
    std::string event_topic;          // 事件发布主题（Thumbler协议）
    std::string system_status_topic;  // 系统状态发布主题
    std::string sensor_status_topic;  // 传感器状态发布主题
    std::string actuator_status_topic;// 执行器状态发布主题
    int keepalive_interval = 60;
    bool use_ssl = false;
    
    UserMqttClientConfig() = default;
    
    UserMqttClientConfig(const std::string& host, int port, const std::string& cid,
                   const std::string& user = "", const std::string& pass = "")
        : broker_host(host), broker_port(port), client_id(cid), 
          username(user), password(pass) {
        // 自动生成主题
        device_info_topic = "device/" + client_id + "/info";
        control_topic = "device/" + client_id + "/control";
        status_topic = "device/" + client_id + "/status";
        system_status_topic = "device/" + client_id + "/status/system";
        sensor_status_topic = "device/" + client_id + "/status/sensor";
        actuator_status_topic = "device/" + client_id + "/status/actuator";
    }
};

// 设备固定配置信息（基本不变）
struct DeviceConfigInfo {
    std::string device_id;
    std::string device_type = "ATK-DNESP32S3";
    std::string firmware_version;
    std::string mac_address;
    std::string chip_model;
    std::string chip_revision;
    
    // 硬件能力（固定属性）
    struct {
        bool camera = false;
        bool can_bus = false;
        bool led_strip = false;
        bool gimbal = false;
        bool arm = false;
        bool motor = false;
        bool sensor = false;
    } capabilities;
    
    DeviceConfigInfo() = default;
};

// 设备状态分类结构
namespace DeviceStatus {
    // 系统动态信息
    struct SystemInfo {
        std::string wifi_ssid;
        std::string ip_address;
        int free_heap;
        int uptime_seconds;
        float cpu_temperature;
        std::string network_status;
    };
    
    // 传感器数据
    struct SensorData {
        float acc_x;                    // X轴加速度 (m/s²)
        float acc_y;                    // Y轴加速度 (m/s²)
        float acc_z;                    // Z轴加速度 (m/s²)
        float acc_g;                    // 总加速度 (m/s²)
        float pitch;                    // 俯仰角 (度)
        float roll;                     // 翻滚角 (度)
        std::string sensor_status;
    };
    
    // 执行器状态
    struct ActuatorStatus {
        struct {
            bool connected;
            int motor_count;
            std::string status;
        } arm;
        
        struct {
            bool connected;
            int motor_count;
            std::string status;
        } motor;
    };
    
    // Thumbler 状态（完整状态消息）
    struct ThumblerStatus {
        bool cur_cam_switch = false;           // 摄像头开关状态
        float g_acc_x = 0.0f;                  // X轴加速度 (m/s²)
        float g_acc_y = 0.0f;                  // Y轴加速度 (m/s²)
        float g_acc_z = 0.0f;                  // Z轴加速度 (m/s²)
        float g_acc_g = 0.0f;                  // 总加速度 (m/s²)
        float g_pitch = 0.0f;                  // 俯仰角 (度)
        float g_roll = 0.0f;                   // 翻滚角 (度)
        int cur_led_mode = 0;                  // 当前LED工作模式
        int cur_led_brightness = 0;            // 当前LED默认亮度
        int cur_led_low_brightness = 0;        // 当前LED低亮度
        int cur_led_color_red = 0;             // 当前LED颜色-红色分量
        int cur_led_color_green = 0;           // 当前LED颜色-绿色分量
        int cur_led_color_blue = 0;            // 当前LED颜色-蓝色分量
        int cur_led_interval_ms = 0;           // 当前LED动画间隔时间
        int cur_led_scroll_length = 0;         // 当前LED滚动模式下的亮灯数量
        int cur_tumbler_mode = 0;              // 不倒翁工作模式
        bool is_has_people = false;            // 当前环境是否有人
        int power_percent = 0;                 // 当前系统电量 (0-100)
    };
}

// 远程控制命令结构体（旧格式，保留兼容性）
struct RemoteControlCommand {
    std::string command_type;
    std::string target;
    std::string action;
    cJSON* parameters = nullptr;
    
    ~RemoteControlCommand() {
        if (parameters) {
            cJSON_Delete(parameters);
        }
    }
};

// Thumbler 控制命令结构体（新格式）
struct ThumblerControlCommand {
    // 基础控制字段
    bool has_tar_cam_switch = false;
    bool tar_cam_switch = false;
    
    bool has_tar_pitch = false;
    float tar_pitch = 0.0f;
    
    bool has_tar_roll = false;
    float tar_roll = 0.0f;
    
    bool has_tar_tumbler_mode = false;
    int tar_tumbler_mode = 0;
    
    // LED 控制字段
    bool has_tar_led_mode = false;
    int tar_led_mode = 0;
    
    bool has_tar_led_brightness = false;
    int tar_led_brightness = 128;
    
    bool has_tar_led_low_brightness = false;
    int tar_led_low_brightness = 16;
    
    bool has_tar_led_color_red = false;
    int tar_led_color_red = 0;
    
    bool has_tar_led_color_green = false;
    int tar_led_color_green = 0;
    
    bool has_tar_led_color_blue = false;
    int tar_led_color_blue = 0;
    
    bool has_tar_led_color_low_red = false;
    int tar_led_color_low_red = 0;
    
    bool has_tar_led_color_low_green = false;
    int tar_led_color_low_green = 0;
    
    bool has_tar_led_color_low_blue = false;
    int tar_led_color_low_blue = 0;
    
    bool has_tar_led_interval_ms = false;
    int tar_led_interval_ms = 500;
    
    bool has_tar_led_scroll_length = false;
    int tar_led_scroll_length = 3;
};

// 用户MQTT客户端类
class UserMqttClient {
public:
    UserMqttClient();
    ~UserMqttClient();
    
    // 初始化和连接
    bool Initialize(const UserMqttClientConfig& config);
    bool Connect();
    void Disconnect();
    bool IsConnected() const;
    
    // 状态发送
    bool SendStatus(const std::string& status, const std::string& message = "");
    bool SendHeartbeat();
    
    // 新的分级发送方法
    bool SendDeviceConfig(const DeviceConfigInfo& config);
    bool SendSystemStatus(const DeviceStatus::SystemInfo& system_info);
    bool SendSensorStatus(const DeviceStatus::SensorData& sensor_data);
    bool SendActuatorStatus(const DeviceStatus::ActuatorStatus& actuator_status);
    
    // Thumbler 协议发送方法
    bool SendThumblerStatus(const DeviceStatus::ThumblerStatus& status);
    bool SendEvent(const std::string& event_type, const std::string& message, const cJSON* data = nullptr);
    
    // 远程控制回调设置
    void SetControlCallback(std::function<void(const RemoteControlCommand&)> callback);
    void SetThumblerControlCallback(std::function<void(const ThumblerControlCommand&)> callback);
    void SetConnectionCallback(std::function<void(bool)> callback);
    
    // 配置管理
    void UpdateConfig(const UserMqttClientConfig& config);
    UserMqttClientConfig GetConfig() const;
    
    // 状态查询
    std::string GetLastError() const;
    int GetConnectionRetryCount() const;
    
private:
    UserMqttClientConfig config_;
    std::unique_ptr<Mqtt> mqtt_client_;
    EventGroupHandle_t event_group_;
    esp_timer_handle_t heartbeat_timer_;
    esp_timer_handle_t reconnect_timer_;
    
    // 状态变量
    bool connected_;
    bool initialized_;
    std::string last_error_;
    int retry_count_;
    uint32_t last_heartbeat_time_;
    
    // 回调函数
    std::function<void(const RemoteControlCommand&)> control_callback_;
    std::function<void(const ThumblerControlCommand&)> thumbler_control_callback_;
    std::function<void(bool)> connection_callback_;
    
    // 内部方法
    bool CreateMqttClient();
    void SetupCallbacks();
    void ParseControlMessage(const std::string& topic, const std::string& payload);
    RemoteControlCommand ParseCommand(const cJSON* json);
    ThumblerControlCommand ParseThumblerCommand(const cJSON* json);
    std::string StatusToJson(const std::string& status, const std::string& message);
    
    // 定时器回调
    static void HeartbeatTimerCallback(void* arg);
    static void ReconnectTimerCallback(void* arg);
    
    // MQTT事件回调
    void OnConnected();
    void OnDisconnected();
    void OnMessage(const std::string& topic, const std::string& payload);
    
    // 常量定义
    static constexpr int MQTT_CONNECT_TIMEOUT_MS = 10000;
    static constexpr int MQTT_RECONNECT_INTERVAL_MS = 30000;
    static constexpr int HEARTBEAT_INTERVAL_MS = 60000;
    static constexpr int MAX_RETRY_COUNT = 5;
    
    // 事件位定义
    static constexpr EventBits_t MQTT_CONNECTED_BIT = BIT0;
    static constexpr EventBits_t MQTT_DISCONNECTED_BIT = BIT1;
    static constexpr EventBits_t MQTT_ERROR_BIT = BIT2;
};

#endif // USER_MQTT_CLIENT_H

