#include "mqtt/modules/device_datapoints.h"

#include "mqtt/mqtt_client.h"
#include "mqtt/mqtt_config.h"
#include "system_info.h"

#include <esp_app_desc.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <wifi_manager.h>

#include <climits>
#include <cstdio>

#define TAG "dev_dp"

// ============================================================
// 上下文
// ============================================================

static DeepDogMqttClient* s_client = nullptr;
static int s_http_port = 0;
static int s_ws_mcp_port = 0;
static const char* s_ws_mcp_path = "";

// ============================================================
// 读回调（info 类 / 静态）
// ============================================================

static const char* dp_dev_read_device_id(void* ctx) {
    (void)ctx;
    if (!s_client) return "";
    return s_client->settings().device_id.c_str();
}

static const char* dp_dev_read_firmware(void* ctx) {
    (void)ctx;
    const esp_app_desc_t* app = esp_app_get_description();
    if (app && app->version[0] != '\0') {
        return app->version;
    }
    return "0.0.0";
}

static const char* dp_dev_read_board(void* ctx) {
    (void)ctx;
    return BOARD_NAME;
}

static const char* dp_dev_read_chip_model(void* ctx) {
    (void)ctx;
    // 返回静态字符串（SystemInfo::GetChipModelName 返回 std::string，这里用静态 buffer）
    static char s_buf[32];
    static bool s_init = false;
    if (!s_init) {
        std::string name = SystemInfo::GetChipModelName();
        snprintf(s_buf, sizeof(s_buf), "%s", name.c_str());
        s_init = true;
    }
    return s_buf;
}

static const char* dp_dev_read_idf_version(void* ctx) {
    (void)ctx;
    const esp_app_desc_t* app = esp_app_get_description();
    if (app && app->idf_ver[0] != '\0') {
        return app->idf_ver;
    }
    return "";
}

static int32_t dp_dev_read_flash_size(void* ctx) {
    (void)ctx;
    return static_cast<int32_t>(SystemInfo::GetFlashSize());
}

static const char* dp_dev_read_mac(void* ctx) {
    (void)ctx;
    static char s_buf[18];
    static bool s_init = false;
    if (!s_init) {
        uint8_t mac[6] = {};
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(s_buf, sizeof(s_buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        s_init = true;
    }
    return s_buf;
}

static const char* dp_dev_read_reset_reason(void* ctx) {
    (void)ctx;
    static char s_buf[16];
    static bool s_init = false;
    if (!s_init) {
        switch (esp_reset_reason()) {
            case ESP_RST_POWERON:  snprintf(s_buf, sizeof(s_buf), "poweron"); break;
            case ESP_RST_EXT:      snprintf(s_buf, sizeof(s_buf), "external"); break;
            case ESP_RST_SW:       snprintf(s_buf, sizeof(s_buf), "software"); break;
            case ESP_RST_PANIC:    snprintf(s_buf, sizeof(s_buf), "panic"); break;
            case ESP_RST_DEEPSLEEP: snprintf(s_buf, sizeof(s_buf), "deepsleep"); break;
            case ESP_RST_BROWNOUT: snprintf(s_buf, sizeof(s_buf), "brownout"); break;
            default:               snprintf(s_buf, sizeof(s_buf), "unknown"); break;
        }
        s_init = true;
    }
    return s_buf;
}

// ============================================================
// 读回调（status 类 / 动态）
// ============================================================

static bool dp_dev_read_online(void* ctx) {
    (void)ctx;
    return s_client && s_client->IsConnected();
}

static int32_t dp_dev_read_uptime_s(void* ctx) {
    (void)ctx;
    return static_cast<int32_t>(esp_timer_get_time() / 1000000LL);
}

static int32_t dp_dev_read_free_heap(void* ctx) {
    (void)ctx;
    return static_cast<int32_t>(esp_get_free_heap_size());
}

static const char* dp_dev_read_ip(void* ctx) {
    (void)ctx;
    static char s_buf[16];
    std::string ip = WifiManager::GetInstance().GetIpAddress();
    if (!ip.empty()) {
        snprintf(s_buf, sizeof(s_buf), "%s", ip.c_str());
        return s_buf;
    }
    return "";
}

static int32_t dp_dev_read_rssi(void* ctx) {
    (void)ctx;
    wifi_ap_record_t ap{};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        return static_cast<int32_t>(ap.rssi);
    }
    return 0;
}

// ============================================================
// 数据点定义表（12 个）
// ============================================================

static const DataPointDef kDeviceDataPoints[kDeviceDataPointCount] = {
    // ---- info 类（retain，静态/准静态） ----
    {
        "device.device_id",
        "设备 ID",
        "device",
        nullptr,
        "设备唯一标识符",
        DP_TYPE_STRING,
        "",
        0,
        0, 0, 0,
        nullptr,
        DP_ACCESS_RO,
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,
        DP_POLICY_RETAIN,
        0,
        0,
        nullptr, nullptr,
        { .read_string = dp_dev_read_device_id },
        { nullptr },
    },
    {
        "device.firmware",
        "固件版本",
        "device",
        nullptr,
        "当前固件版本号",
        DP_TYPE_STRING,
        "",
        0,
        0, 0, 0,
        nullptr,
        DP_ACCESS_RO,
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,
        DP_POLICY_RETAIN,
        0,
        0,
        nullptr, nullptr,
        { .read_string = dp_dev_read_firmware },
        { nullptr },
    },
    {
        "device.board",
        "板型",
        "device",
        nullptr,
        "硬件板型名称",
        DP_TYPE_STRING,
        "",
        0,
        0, 0, 0,
        nullptr,
        DP_ACCESS_RO,
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,
        DP_POLICY_RETAIN,
        0,
        0,
        nullptr, nullptr,
        { .read_string = dp_dev_read_board },
        { nullptr },
    },
    {
        "device.chip_model",
        "芯片型号",
        "device",
        nullptr,
        "芯片型号名称",
        DP_TYPE_STRING,
        "",
        0,
        0, 0, 0,
        nullptr,
        DP_ACCESS_RO,
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,
        DP_POLICY_RETAIN,
        0,
        0,
        nullptr, nullptr,
        { .read_string = dp_dev_read_chip_model },
        { nullptr },
    },
    {
        "device.idf_version",
        "IDF 版本",
        "device",
        nullptr,
        "ESP-IDF 版本",
        DP_TYPE_STRING,
        "",
        0,
        0, 0, 0,
        nullptr,
        DP_ACCESS_RO,
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,
        DP_POLICY_RETAIN,
        0,
        0,
        nullptr, nullptr,
        { .read_string = dp_dev_read_idf_version },
        { nullptr },
    },
    {
        "device.flash_size",
        "Flash 大小",
        "device",
        nullptr,
        "Flash 总字节数",
        DP_TYPE_INT,
        "bytes",
        0,
        0, 16777216.0f, 1,
        nullptr,
        DP_ACCESS_RO,
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,
        DP_POLICY_RETAIN,
        0,
        0,
        nullptr, nullptr,
        { .read_int = dp_dev_read_flash_size },
        { nullptr },
    },
    {
        "device.mac",
        "MAC 地址",
        "device",
        nullptr,
        "WiFi STA MAC 地址",
        DP_TYPE_STRING,
        "",
        0,
        0, 0, 0,
        nullptr,
        DP_ACCESS_RO,
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,
        DP_POLICY_RETAIN,
        0,
        0,
        nullptr, nullptr,
        { .read_string = dp_dev_read_mac },
        { nullptr },
    },
    {
        "device.reset_reason",
        "复位原因",
        "device",
        nullptr,
        "本次启动的复位原因",
        DP_TYPE_STRING,
        "",
        0,
        0, 0, 0,
        nullptr,
        DP_ACCESS_RO,
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,
        DP_POLICY_RETAIN,
        0,
        0,
        nullptr, nullptr,
        { .read_string = dp_dev_read_reset_reason },
        { nullptr },
    },
    // ---- status 类（动态） ----
    {
        "device.online",
        "在线状态",
        "device",
        nullptr,
        "设备是否在线（MQTT 连接状态）",
        DP_TYPE_BOOL,
        "",
        0,
        0, 0, 0,
        nullptr,
        DP_ACCESS_RO,
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,
        DP_POLICY_ON_CHANGE,
        0,
        0,
        nullptr, nullptr,
        { .read_bool = dp_dev_read_online },
        { nullptr },
    },
    {
        "device.uptime_s",
        "运行时间",
        "device",
        nullptr,
        "设备启动以来的秒数",
        DP_TYPE_INT,
        "s",
        0,
        0, 2147483647.0f, 1,
        nullptr,
        DP_ACCESS_RO,
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP | DP_CH_TELEMETRY,
        DP_POLICY_PERIODIC,
        30000,
        0,
        nullptr, nullptr,
        { .read_int = dp_dev_read_uptime_s },
        { nullptr },
    },
    {
        "device.free_heap",
        "空闲内存",
        "device",
        nullptr,
        "当前空闲堆内存字节数",
        DP_TYPE_INT,
        "bytes",
        0,
        0, 8388608.0f, 1,
        nullptr,
        DP_ACCESS_RO,
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP | DP_CH_TELEMETRY,
        DP_POLICY_PERIODIC,
        30000,
        1024.0f,
        nullptr, nullptr,
        { .read_int = dp_dev_read_free_heap },
        { nullptr },
    },
    {
        "device.ip",
        "IP 地址",
        "device",
        nullptr,
        "当前 WiFi IP 地址",
        DP_TYPE_STRING,
        "",
        0,
        0, 0, 0,
        nullptr,
        DP_ACCESS_RO,
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,
        DP_POLICY_ON_CHANGE,
        0,
        0,
        nullptr, nullptr,
        { .read_string = dp_dev_read_ip },
        { nullptr },
    },
    // rssi（telemetry：与原手写 telemetry 字段 rssi 对齐）
    {
        "device.rssi",
        "WiFi 信号强度",
        "device",
        nullptr,
        "WiFi 接收信号强度指示（dBm）",
        DP_TYPE_INT,
        "dBm",
        0,
        -100.0f, 0.0f, 1,
        nullptr,
        DP_ACCESS_RO,
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP | DP_CH_TELEMETRY,
        DP_POLICY_PERIODIC,
        30000,
        2.0f,
        nullptr, nullptr,
        { .read_int = dp_dev_read_rssi },
        { nullptr },
    },
};

// ============================================================
// 模块定义
// ============================================================

static const char* kDeviceModuleDisplayName = "设备信息";
static const char* kDeviceModuleDescription = "设备身份、固件版本、连接状态等基础信息";

static const DataModuleDef kDeviceModuleDef = {
    "device",
    kDeviceModuleDisplayName,
    kDeviceModuleDescription,
    kDeviceDataPoints,
    kDeviceDataPointCount,
    false,
};

// ============================================================
// 公共 API
// ============================================================

void DeviceDataPoints_Register(DeepDogMqttClient* client) {
    s_client = client;
    auto& reg = DataPointRegistry::Instance();
    bool ok = reg.RegisterModule(kDeviceModuleDef);
    ESP_LOGI(TAG, "register device data points: ok=%d count=%u", ok ? 1 : 0,
             (unsigned)kDeviceDataPointCount);
}

const DataModuleDef* DeviceDataPoints_GetModuleDef() {
    return &kDeviceModuleDef;
}
