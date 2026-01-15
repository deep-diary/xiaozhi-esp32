#include "user_mqtt_config.h"
#include "settings.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"
#include <sstream>
#include <iomanip>

#define TAG_USER_CONFIG "UserMqttConfig"

// 静态成员变量定义
std::string UserMqttConfig::broker_host_ = DEFAULT_BROKER_HOST;
int UserMqttConfig::broker_port_ = DEFAULT_BROKER_PORT;
std::string UserMqttConfig::client_id_ = "";
std::string UserMqttConfig::username_ = "";
std::string UserMqttConfig::password_ = "";
int UserMqttConfig::keepalive_interval_ = DEFAULT_KEEPALIVE_INTERVAL;
bool UserMqttConfig::use_ssl_ = DEFAULT_USE_SSL;
bool UserMqttConfig::initialized_ = false;

void UserMqttConfig::SetBrokerHost(const std::string& host) {
    InitializeIfNeeded();
    broker_host_ = host;
    ESP_LOGI(TAG_USER_CONFIG, "Broker host set to: %s", host.c_str());
}

void UserMqttConfig::SetBrokerPort(int port) {
    InitializeIfNeeded();
    broker_port_ = port;
    ESP_LOGI(TAG_USER_CONFIG, "Broker port set to: %d", port);
}

void UserMqttConfig::SetClientId(const std::string& client_id) {
    InitializeIfNeeded();
    client_id_ = client_id;
    ESP_LOGI(TAG_USER_CONFIG, "Client ID set to: %s", client_id.c_str());
}

void UserMqttConfig::SetUsername(const std::string& username) {
    InitializeIfNeeded();
    username_ = username;
    ESP_LOGI(TAG_USER_CONFIG, "Username set to: %s", username.c_str());
}

void UserMqttConfig::SetPassword(const std::string& password) {
    InitializeIfNeeded();
    password_ = password;
    ESP_LOGI(TAG_USER_CONFIG, "Password set");
}

void UserMqttConfig::SetKeepaliveInterval(int interval) {
    InitializeIfNeeded();
    keepalive_interval_ = interval;
    ESP_LOGI(TAG_USER_CONFIG, "Keepalive interval set to: %d", interval);
}

void UserMqttConfig::SetUseSsl(bool use_ssl) {
    InitializeIfNeeded();
    use_ssl_ = use_ssl;
    ESP_LOGI(TAG_USER_CONFIG, "SSL usage set to: %s", use_ssl ? "true" : "false");
}

std::string UserMqttConfig::GetBrokerHost() {
    InitializeIfNeeded();
    return broker_host_;
}

int UserMqttConfig::GetBrokerPort() {
    InitializeIfNeeded();
    return broker_port_;
}

std::string UserMqttConfig::GetClientId() {
    InitializeIfNeeded();
    if (client_id_.empty()) {
        // 生成默认客户端ID
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        
        std::stringstream ss;
        ss << "ATK-DNESP32S3-";
        for (int i = 0; i < 6; i++) {
            ss << std::hex << std::setfill('0') << std::setw(2) << (int)mac[i];
        }
        client_id_ = ss.str();
    }
    return client_id_;
}

std::string UserMqttConfig::GetUsername() {
    InitializeIfNeeded();
    return username_;
}

std::string UserMqttConfig::GetPassword() {
    InitializeIfNeeded();
    return password_;
}

int UserMqttConfig::GetKeepaliveInterval() {
    InitializeIfNeeded();
    return keepalive_interval_;
}

bool UserMqttConfig::GetUseSsl() {
    InitializeIfNeeded();
    return use_ssl_;
}

void UserMqttConfig::ResetToDefaults() {
    broker_host_ = DEFAULT_BROKER_HOST;
    broker_port_ = DEFAULT_BROKER_PORT;
    client_id_ = "";
    username_ = "";
    password_ = "";
    keepalive_interval_ = DEFAULT_KEEPALIVE_INTERVAL;
    use_ssl_ = DEFAULT_USE_SSL;
    initialized_ = true;
    
    ESP_LOGI(TAG_USER_CONFIG, "Configuration reset to defaults");
}

void UserMqttConfig::SaveToNvs() {
    InitializeIfNeeded();
    
    Settings settings("user_mqtt", true);
    settings.SetString("broker_host", broker_host_);
    settings.SetInt("broker_port", broker_port_);
    settings.SetString("client_id", client_id_);
    settings.SetString("username", username_);
    settings.SetString("password", password_);
    settings.SetInt("keepalive_interval", keepalive_interval_);
    settings.SetBool("use_ssl", use_ssl_);
    
    ESP_LOGI(TAG_USER_CONFIG, "Configuration saved to NVS");
}

void UserMqttConfig::LoadFromNvs() {
    Settings settings("user_mqtt", false);
    
    broker_host_ = settings.GetString("broker_host", DEFAULT_BROKER_HOST);
    broker_port_ = settings.GetInt("broker_port", DEFAULT_BROKER_PORT);
    client_id_ = settings.GetString("client_id", "");
    username_ = settings.GetString("username", "");
    password_ = settings.GetString("password", "");
    keepalive_interval_ = settings.GetInt("keepalive_interval", DEFAULT_KEEPALIVE_INTERVAL);
    use_ssl_ = settings.GetBool("use_ssl", DEFAULT_USE_SSL);
    
    initialized_ = true;
    
    ESP_LOGI(TAG_USER_CONFIG, "Configuration loaded from NVS");
    ESP_LOGI(TAG_USER_CONFIG, "Broker: %s:%d, Client ID: %s", 
             broker_host_.c_str(), broker_port_, client_id_.c_str());
}

void UserMqttConfig::InitializeIfNeeded() {
    if (!initialized_) {
        LoadFromNvs();
    }
}

