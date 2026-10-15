#include "mqtt/modules/led_datapoints.h"

#include "led/led_strip_control.h"
#include "led/led_config.h"

#include <climits>
#include <cstring>

#define TAG "led_dp"

// ============================================================
// 读回调（从 LedStripControl 读取当前值）
// ============================================================

static LedStripControl* s_ctrl = nullptr;

static int32_t dp_led_read_mode(void* ctx) {
    (void)ctx;
    if (!s_ctrl) return DEEP_DOG_LED_MODE_OFF;
    return s_ctrl->GetStatus().mode;
}

static int32_t dp_led_read_brightness(void* ctx) {
    (void)ctx;
    if (!s_ctrl) return 0;
    return s_ctrl->GetStatus().brightness;
}

static int32_t dp_led_read_low_brightness(void* ctx) {
    (void)ctx;
    if (!s_ctrl) return 0;
    return s_ctrl->GetStatus().low_brightness;
}

static int32_t dp_led_read_r(void* ctx) {
    (void)ctx;
    if (!s_ctrl) return 0;
    return s_ctrl->GetStatus().color.red;
}

static int32_t dp_led_read_g(void* ctx) {
    (void)ctx;
    if (!s_ctrl) return 0;
    return s_ctrl->GetStatus().color.green;
}

static int32_t dp_led_read_b(void* ctx) {
    (void)ctx;
    if (!s_ctrl) return 0;
    return s_ctrl->GetStatus().color.blue;
}

static int32_t dp_led_read_low_r(void* ctx) {
    (void)ctx;
    if (!s_ctrl) return 0;
    return s_ctrl->GetStatus().low_color.red;
}

static int32_t dp_led_read_low_g(void* ctx) {
    (void)ctx;
    if (!s_ctrl) return 0;
    return s_ctrl->GetStatus().low_color.green;
}

static int32_t dp_led_read_low_b(void* ctx) {
    (void)ctx;
    if (!s_ctrl) return 0;
    return s_ctrl->GetStatus().low_color.blue;
}

static int32_t dp_led_read_interval_ms(void* ctx) {
    (void)ctx;
    if (!s_ctrl) return 0;
    return s_ctrl->GetStatus().interval_ms;
}

static int32_t dp_led_read_scroll_length(void* ctx) {
    (void)ctx;
    if (!s_ctrl) return 0;
    return s_ctrl->GetStatus().scroll_length;
}

static int32_t dp_led_read_led_count(void* ctx) {
    (void)ctx;
    if (!s_ctrl) return 0;
    return s_ctrl->GetStatus().led_count;
}

static bool dp_led_read_ok(void* ctx) {
    (void)ctx;
    return s_ctrl && s_ctrl->ok();
}

// ============================================================
// 写回调（写入 LedStripControl）
// 注意：颜色/亮度等参数需要结合当前 mode 重放效果，
// 为保持简单，第一版写回调只做状态更新 + NotifyChanged，
// 完整的 mode 切换逻辑仍由 led_mqtt.cc 的 OnMessage 处理。
// 后续版本可把 Apply* 逻辑下沉到这里。
// ============================================================

static bool dp_led_write_brightness(void* ctx, int32_t value) {
    (void)ctx;
    if (!s_ctrl) return false;
    uint8_t b = (value < 0) ? 0 : (value > 255) ? 255 : static_cast<uint8_t>(value);
    uint8_t lb = s_ctrl->GetStatus().low_brightness;
    s_ctrl->SetBrightness(b, lb);
    // 如果是手动模式，需要重放效果以让新亮度生效
    // 这里先只改亮度，后续 OnMessage 的逻辑会处理重放
    // 数据点写入保持轻量：只改状态，触发通知
    return true;
}

// ============================================================
// 数据点定义表
// ============================================================

static DataPointDef kLedDataPoints[] = {
    // --- 模式 ---
    {
        "led.mode", "模式", "led", nullptr,
        "LED 灯效模式：0=关 1=静态 2=闪烁 3=呼吸 4=滚动 5=系统",
        DP_TYPE_INT, "", 0, 0, 5, 1, nullptr,
        DP_ACCESS_RW,
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,
        DP_POLICY_RETAIN, 0, 0,
        nullptr, nullptr,
        { .read_int = dp_led_read_mode },
        { .write_int = nullptr }  // 模式写入需要复杂逻辑，暂不通过数据点写
    },

    // --- 主亮度 ---
    {
        "led.brightness", "主亮度", "led", nullptr, "主模式下 LED 亮度（0-255）",
        DP_TYPE_INT, "", 0, 0, 255, 1, nullptr,
        DP_ACCESS_RW,
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,
        DP_POLICY_RETAIN, 0, 0,
        nullptr, nullptr,
        { .read_int = dp_led_read_brightness },
        { .write_int = dp_led_write_brightness }
    },

    // --- 低亮度 ---
    {
        "led.low_brightness", "低亮度", "led", nullptr, "呼吸/滚动模式下的低电平亮度（0-255）",
        DP_TYPE_INT, "", 0, 0, 255, 1, nullptr,
        DP_ACCESS_RW,
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,
        DP_POLICY_RETAIN, 0, 0,
        nullptr, nullptr,
        { .read_int = dp_led_read_low_brightness },
        { .write_int = nullptr }  // 暂不单独写
    },

    // --- 主颜色 R ---
    {
        "led.r", "主红色", "led", nullptr, "主模式红色分量（0-255）",
        DP_TYPE_INT, "", 0, 0, 255, 1, nullptr,
        DP_ACCESS_RW,
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,
        DP_POLICY_RETAIN, 0, 0,
        nullptr, nullptr,
        { .read_int = dp_led_read_r },
        { .write_int = nullptr }
    },
    {
        "led.g", "主绿色", "led", nullptr, "主模式绿色分量（0-255）",
        DP_TYPE_INT, "", 0, 0, 255, 1, nullptr,
        DP_ACCESS_RW,
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,
        DP_POLICY_RETAIN, 0, 0,
        nullptr, nullptr,
        { .read_int = dp_led_read_g },
        { .write_int = nullptr }
    },
    {
        "led.b", "主蓝色", "led", nullptr, "主模式蓝色分量（0-255）",
        DP_TYPE_INT, "", 0, 0, 255, 1, nullptr,
        DP_ACCESS_RW,
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,
        DP_POLICY_RETAIN, 0, 0,
        nullptr, nullptr,
        { .read_int = dp_led_read_b },
        { .write_int = nullptr }
    },

    // --- 低颜色 ---
    {
        "led.low_r", "低红色", "led", nullptr, "呼吸/滚动低电平红色分量（0-255）",
        DP_TYPE_INT, "", 0, 0, 255, 1, nullptr,
        DP_ACCESS_RW,
        DP_CH_MQTT | DP_CH_HTTP,
        DP_POLICY_RETAIN, 0, 0,
        nullptr, nullptr,
        { .read_int = dp_led_read_low_r },
        { .write_int = nullptr }
    },
    {
        "led.low_g", "低绿色", "led", nullptr, "呼吸/滚动低电平绿色分量（0-255）",
        DP_TYPE_INT, "", 0, 0, 255, 1, nullptr,
        DP_ACCESS_RW,
        DP_CH_MQTT | DP_CH_HTTP,
        DP_POLICY_RETAIN, 0, 0,
        nullptr, nullptr,
        { .read_int = dp_led_read_low_g },
        { .write_int = nullptr }
    },
    {
        "led.low_b", "低蓝色", "led", nullptr, "呼吸/滚动低电平蓝色分量（0-255）",
        DP_TYPE_INT, "", 0, 0, 255, 1, nullptr,
        DP_ACCESS_RW,
        DP_CH_MQTT | DP_CH_HTTP,
        DP_POLICY_RETAIN, 0, 0,
        nullptr, nullptr,
        { .read_int = dp_led_read_low_b },
        { .write_int = nullptr }
    },

    // --- 动画参数 ---
    {
        "led.interval_ms", "动画间隔", "led", nullptr, "闪烁/呼吸/滚动动画间隔（毫秒）",
        DP_TYPE_INT, "ms", 0, 0, 2147483647.0f, 1, nullptr,
        DP_ACCESS_RW,
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,
        DP_POLICY_RETAIN, 0, 0,
        nullptr, nullptr,
        { .read_int = dp_led_read_interval_ms },
        { .write_int = nullptr }
    },
    {
        "led.scroll_length", "滚动长度", "led", nullptr, "滚动模式下的 LED 数量",
        DP_TYPE_INT, "", 0, 1, 2147483647.0f, 1, nullptr,
        DP_ACCESS_RW,
        DP_CH_MQTT | DP_CH_HTTP,
        DP_POLICY_RETAIN, 0, 0,
        nullptr, nullptr,
        { .read_int = dp_led_read_scroll_length },
        { .write_int = nullptr }
    },

    // --- 硬件信息 ---
    {
        "led.led_count", "LED 数量", "led", nullptr, "灯带总 LED 颗数",
        DP_TYPE_INT, "颗", 0, 0, 2147483647.0f, 1, nullptr,
        DP_ACCESS_RO,
        DP_CH_MQTT | DP_CH_HTTP,
        DP_POLICY_RETAIN, 0, 0,
        nullptr, nullptr,
        { .read_int = dp_led_read_led_count },
        { .write_int = nullptr }
    },
    {
        "led.ok", "就绪状态", "led", nullptr, "LED 控制器是否就绪",
        DP_TYPE_BOOL, "", 0, 0, 0, 0, nullptr,
        DP_ACCESS_RO,
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,
        DP_POLICY_RETAIN, 0, 0,
        nullptr, nullptr,
        { .read_bool = dp_led_read_ok },
        { .write_bool = nullptr }
    },
};

static const size_t kLedDataPointCount = sizeof(kLedDataPoints) / sizeof(kLedDataPoints[0]);

// ============================================================
// 模块定义
// ============================================================

static DataModuleDef kLedModuleDef = {
    "led",                                          // name
    kLedModuleDisplayName,                          // display_name
    kLedModuleDescription,                          // description
    kLedDataPoints,                                 // points
    kLedDataPointCount,                             // point_count
    false,                                          // has_sub_instances
};

// ============================================================
// 公共 API
// ============================================================

void LedDataPoints_GetDefs(LedStripControl* ctrl,
                           const DataPointDef** defs,
                           size_t* count) {
    s_ctrl = ctrl;
    if (defs) *defs = kLedDataPoints;
    if (count) *count = kLedDataPointCount;
}

const DataModuleDef* LedDataPoints_GetModuleDef() {
    return &kLedModuleDef;
}
