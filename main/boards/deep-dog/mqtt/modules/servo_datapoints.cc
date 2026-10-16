#include "mqtt/modules/servo_datapoints.h"

#include "servo/servo_control.h"

#include <esp_log.h>

#include <climits>
#include <cstdlib>

#define TAG "servo_dp"

// ============================================================
// 读回调（ctx = sub_id 字符串，如 "0" / "1"）
// ============================================================

namespace {

int CtxToIndex(void* ctx) {
    if (!ctx) return -1;
    return atoi(static_cast<const char*>(ctx));
}

bool ReadSnapshot(void* ctx, DeepDogServoSnapshot* out) {
    const int idx = CtxToIndex(ctx);
    if (idx < 0) return false;
    return DeepDogServoGetSnapshot(idx, out);
}

}  // namespace

#define DP_SERVO_INT_READER(fn, field)                        \
    static int32_t fn(void* ctx) {                            \
        DeepDogServoSnapshot s{};                             \
        if (!ReadSnapshot(ctx, &s)) return 0;                 \
        return static_cast<int32_t>(s.field);                 \
    }

#define DP_SERVO_BOOL_READER(fn, field)                       \
    static bool fn(void* ctx) {                               \
        DeepDogServoSnapshot s{};                             \
        if (!ReadSnapshot(ctx, &s)) return false;             \
        return static_cast<bool>(s.field);                    \
    }

DP_SERVO_INT_READER(dp_servo_read_angle, angle)
DP_SERVO_INT_READER(dp_servo_read_target, target)
DP_SERVO_BOOL_READER(dp_servo_read_attached, attached)
DP_SERVO_BOOL_READER(dp_servo_read_moving, moving)
DP_SERVO_INT_READER(dp_servo_read_type, type)
DP_SERVO_INT_READER(dp_servo_read_min_angle, min_angle)
DP_SERVO_INT_READER(dp_servo_read_max_angle, max_angle)

// 写回调（Phase E）：写入目标角度 = 让舵机移动。ctx = sub_id（实例号）
static bool dp_servo_write_target(void* ctx, int32_t v) {
    const int idx = CtxToIndex(ctx);
    if (idx < 0) return false;
    return DeepDogServoSetAngle(idx, static_cast<int>(v), 0) == ESP_OK;
}

// ============================================================
// 模板点定义（id 用 module.point_name，注册时按 sub_id 展开）
// ============================================================

static const DataPointDef kServoTemplatePoints[kServoPointPerInstance] = {
    {
        "servo.angle", "当前角度", "servo", nullptr, "舵机当前角度",
        DP_TYPE_INT, "°", 0, -360.0f, 360.0f, 1,
        nullptr,
        DP_ACCESS_RO, DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,
        DP_POLICY_ON_CHANGE, 0, 0,
        nullptr, nullptr,
        { .read_int = dp_servo_read_angle }, { nullptr },
    },
    {
        "servo.target", "目标角度", "servo", nullptr, "舵机目标角度（可写，写入即移动）",
        DP_TYPE_INT, "°", 0, -360.0f, 360.0f, 1,
        nullptr,
        DP_ACCESS_RW, DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,
        DP_POLICY_ON_CHANGE, 0, 0,
        nullptr, nullptr,
        { .read_int = dp_servo_read_target }, { .write_int = dp_servo_write_target },
    },
    {
        "servo.attached", "已挂载", "servo", nullptr, "舵机是否已挂载",
        DP_TYPE_BOOL, "", 0, 0, 0, 0,
        nullptr,
        DP_ACCESS_RO, DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,
        DP_POLICY_ON_CHANGE, 0, 0,
        nullptr, nullptr,
        { .read_bool = dp_servo_read_attached }, { nullptr },
    },
    {
        "servo.moving", "运动中", "servo", nullptr, "舵机是否在运动",
        DP_TYPE_BOOL, "", 0, 0, 0, 0,
        nullptr,
        DP_ACCESS_RO, DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,
        DP_POLICY_ON_CHANGE, 0, 0,
        nullptr, nullptr,
        { .read_bool = dp_servo_read_moving }, { nullptr },
    },
    {
        "servo.type", "舵机类型", "servo", nullptr, "舵机类型（90/180/270/360）",
        DP_TYPE_INT, "°", 0, 0.0f, 360.0f, 1,
        nullptr,
        DP_ACCESS_RO, DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,
        DP_POLICY_RETAIN, 0, 0,
        nullptr, nullptr,
        { .read_int = dp_servo_read_type }, { nullptr },
    },
    {
        "servo.min_angle", "角度下限", "servo", nullptr, "舵机角度下限",
        DP_TYPE_INT, "°", 0, -360.0f, 360.0f, 1,
        nullptr,
        DP_ACCESS_RO, DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,
        DP_POLICY_RETAIN, 0, 0,
        nullptr, nullptr,
        { .read_int = dp_servo_read_min_angle }, { nullptr },
    },
    {
        "servo.max_angle", "角度上限", "servo", nullptr, "舵机角度上限",
        DP_TYPE_INT, "°", 0, -360.0f, 360.0f, 1,
        nullptr,
        DP_ACCESS_RO, DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,
        DP_POLICY_RETAIN, 0, 0,
        nullptr, nullptr,
        { .read_int = dp_servo_read_max_angle }, { nullptr },
    },
};

static const DataModuleDef kServoModuleTemplate = {
    "servo",
    "舵机",
    "舵机角度与状态（多实例）",
    kServoTemplatePoints,
    kServoPointPerInstance,
    true,  // has_sub_instances
};

// ============================================================
// 公共 API
// ============================================================

void ServoDataPoints_Register() {
    const int n = DeepDogServoCount();
    if (n <= 0) {
        ESP_LOGW(TAG, "servo count=0, skip registration");
        return;
    }

    // 构造 sub_id 列表（0-based），registry 会拷贝持久化，故局部数组安全
    const char* sub_ids[8];
    if (n > 8) {
        ESP_LOGW(TAG, "servo count=%d exceeds cap 8, clamp", n);
    }
    const int cnt = (n > 8) ? 8 : n;
    static char id_buf[8][4];
    for (int i = 0; i < cnt; ++i) {
        snprintf(id_buf[i], sizeof(id_buf[i]), "%d", i);
        sub_ids[i] = id_buf[i];
    }

    auto& reg = DataPointRegistry::Instance();
    bool ok = reg.RegisterMultiInstanceModule(kServoModuleTemplate, sub_ids, static_cast<size_t>(cnt));
    ESP_LOGI(TAG, "register servo data points: ok=%d sub=%d x %u points", ok ? 1 : 0, cnt,
             (unsigned)kServoPointPerInstance);
}

const DataModuleDef* ServoDataPoints_GetModuleDef() {
    return &kServoModuleTemplate;
}
