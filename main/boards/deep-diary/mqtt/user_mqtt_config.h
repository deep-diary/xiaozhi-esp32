#ifndef USER_MQTT_CONFIG_H
#define USER_MQTT_CONFIG_H

#include <string>

// 用户MQTT配置类
class UserMqttConfig {
public:
    // 默认配置
    static constexpr const char* DEFAULT_BROKER_HOST = "broker.emqx.io"; //个人服务器：35.192.64.247， 公有云：broker.emqx.io
    static constexpr int DEFAULT_BROKER_PORT = 1883;
    static constexpr int DEFAULT_KEEPALIVE_INTERVAL = 60;
    static constexpr bool DEFAULT_USE_SSL = false;
    
    // 配置方法
    static void SetBrokerHost(const std::string& host);
    static void SetBrokerPort(int port);
    static void SetClientId(const std::string& client_id);
    static void SetUsername(const std::string& username);
    static void SetPassword(const std::string& password);
    static void SetKeepaliveInterval(int interval);
    static void SetUseSsl(bool use_ssl);
    
    // 获取配置
    static std::string GetBrokerHost();
    static int GetBrokerPort();
    static std::string GetClientId();
    static std::string GetUsername();
    static std::string GetPassword();
    static int GetKeepaliveInterval();
    static bool GetUseSsl();
    
    // 重置为默认配置
    static void ResetToDefaults();
    
    // 保存配置到NVS
    static void SaveToNvs();
    
    // 从NVS加载配置
    static void LoadFromNvs();
    
private:
    static std::string broker_host_;
    static int broker_port_;
    static std::string client_id_;
    static std::string username_;
    static std::string password_;
    static int keepalive_interval_;
    static bool use_ssl_;
    static bool initialized_;
    
    static void InitializeIfNeeded();
};

#endif // USER_MQTT_CONFIG_H

