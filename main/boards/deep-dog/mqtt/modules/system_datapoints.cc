#include "mqtt/modules/system_datapoints.h"

#include "system_info.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <climits>
#include <cstdio>

#define TAG "sys_dp"

// ============================================================
// 读回调
// ============================================================

static int32_t dp_sys_read_free_heap_internal(void* ctx) {
    (void)ctx;
    return static_cast<int32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

static int32_t dp_sys_read_min_free_heap_internal(void* ctx) {
    (void)ctx;
    return static_cast<int32_t>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
}

static int32_t dp_sys_read_free_heap_psram(void* ctx) {
    (void)ctx;
    return static_cast<int32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static int32_t dp_sys_read_min_free_heap_psram(void* ctx) {
    (void)ctx;
    return static_cast<int32_t>(heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
}

static int32_t dp_sys_read_largest_free_block_internal(void* ctx) {
    (void)ctx;
    return static_cast<int32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

static int32_t dp_sys_read_largest_free_block_psram(void* ctx) {
    (void)ctx;
    return static_cast<int32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}

static int32_t dp_sys_read_uptime_s(void* ctx) {
    (void)ctx;
    return static_cast<int32_t>(esp_timer_get_time() / 1000000LL);
}

static const char* dp_sys_read_chip_model(void* ctx) {
    (void)ctx;
    static char s_buf[32];
    static bool s_init = false;
    if (!s_init) {
        std::string name = SystemInfo::GetChipModelName();
        snprintf(s_buf, sizeof(s_buf), "%s", name.c_str());
        s_init = true;
    }
    return s_buf;
}

// ============================================================
// 数据点定义表（8 个）
// ============================================================

static const DataPointDef kSystemDataPoints[kSystemDataPointCount] = {
    {
        "system.chip_model",
        "芯片型号",
        "system",
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
        { .read_string = dp_sys_read_chip_model },
        { nullptr },
    },
    {
        "system.uptime_s",
        "运行时间",
        "system",
        nullptr,
        "设备启动以来的秒数",
        DP_TYPE_INT,
        "s",
        0,
        0, 2147483647.0f, 1,
        nullptr,
        DP_ACCESS_RO,
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,  // 不上 telemetry（uptime_s 由 device.uptime_s 提供，避免重复）
        DP_POLICY_PERIODIC,
        30000,
        0,
        nullptr, nullptr,
        { .read_int = dp_sys_read_uptime_s },
        { nullptr },
    },
    {
        "system.internal_free",
        "内部空闲内存",
        "system",
        nullptr,
        "内部 SRAM 空闲字节数",
        DP_TYPE_INT,
        "bytes",
        0,
        0, 524288.0f, 1,
        nullptr,
        DP_ACCESS_RO,
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP | DP_CH_TELEMETRY,
        DP_POLICY_PERIODIC,
        10000,
        1024.0f,
        nullptr, nullptr,
        { .read_int = dp_sys_read_free_heap_internal },
        { nullptr },
    },
    {
        "system.min_free_heap",
        "内部最小空闲",
        "system",
        nullptr,
        "内部 SRAM 历史最小空闲字节数",
        DP_TYPE_INT,
        "bytes",
        0,
        0, 524288.0f, 1,
        nullptr,
        DP_ACCESS_RO,
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP | DP_CH_TELEMETRY,
        DP_POLICY_PERIODIC,
        30000,
        0,
        nullptr, nullptr,
        { .read_int = dp_sys_read_min_free_heap_internal },
        { nullptr },
    },
    {
        "system.largest_free_internal",
        "内部最大空闲块",
        "system",
        nullptr,
        "内部 SRAM 最大连续空闲块字节数",
        DP_TYPE_INT,
        "bytes",
        0,
        0, 524288.0f, 1,
        nullptr,
        DP_ACCESS_RO,
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,
        DP_POLICY_PERIODIC,
        30000,
        0,
        nullptr, nullptr,
        { .read_int = dp_sys_read_largest_free_block_internal },
        { nullptr },
    },
    {
        "system.psram_free",
        "PSRAM 空闲内存",
        "system",
        nullptr,
        "PSRAM 空闲字节数",
        DP_TYPE_INT,
        "bytes",
        0,
        0, 8388608.0f, 1,
        nullptr,
        DP_ACCESS_RO,
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP | DP_CH_TELEMETRY,
        DP_POLICY_PERIODIC,
        10000,
        1024.0f,
        nullptr, nullptr,
        { .read_int = dp_sys_read_free_heap_psram },
        { nullptr },
    },
    {
        "system.min_free_heap_psram",
        "PSRAM 最小空闲",
        "system",
        nullptr,
        "PSRAM 历史最小空闲字节数",
        DP_TYPE_INT,
        "bytes",
        0,
        0, 8388608.0f, 1,
        nullptr,
        DP_ACCESS_RO,
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,
        DP_POLICY_PERIODIC,
        30000,
        0,
        nullptr, nullptr,
        { .read_int = dp_sys_read_min_free_heap_psram },
        { nullptr },
    },
    {
        "system.largest_free_psram",
        "PSRAM 最大空闲块",
        "system",
        nullptr,
        "PSRAM 最大连续空闲块字节数",
        DP_TYPE_INT,
        "bytes",
        0,
        0, 8388608.0f, 1,
        nullptr,
        DP_ACCESS_RO,
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,
        DP_POLICY_PERIODIC,
        30000,
        0,
        nullptr, nullptr,
        { .read_int = dp_sys_read_largest_free_block_psram },
        { nullptr },
    },
};

// ============================================================
// 模块定义
// ============================================================

static const char* kSystemModuleDisplayName = "系统资源";
static const char* kSystemModuleDescription = "内存、芯片型号等系统级观测数据";

static const DataModuleDef kSystemModuleDef = {
    "system",
    kSystemModuleDisplayName,
    kSystemModuleDescription,
    kSystemDataPoints,
    kSystemDataPointCount,
    false,
};

// ============================================================
// 公共 API
// ============================================================

void SystemDataPoints_Register() {
    auto& reg = DataPointRegistry::Instance();
    bool ok = reg.RegisterModule(kSystemModuleDef);
    ESP_LOGI(TAG, "register system data points: ok=%d count=%u", ok ? 1 : 0,
             (unsigned)kSystemDataPointCount);
}

const DataModuleDef* SystemDataPoints_GetModuleDef() {
    return &kSystemModuleDef;
}
