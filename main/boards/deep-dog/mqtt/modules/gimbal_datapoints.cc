#include "mqtt/modules/gimbal_datapoints.h"

#include "gimbal/Gimbal.h"

#include <esp_log.h>

#include <climits>

#define TAG "gimbal_dp"

// ============================================================
// 读回调（统一从 GimbalStatus_t 取值）
// ============================================================

namespace {

bool ReadStatus(GimbalStatus_t* out) {
    if (!DeepDogGimbalReady()) return false;
    return Gimbal_getStatus(DeepDogGimbalGet(), out);
}

}  // namespace

#define DP_GIMBAL_INT_READER(fn, field)                       \
    static int32_t fn(void* ctx) {                            \
        (void)ctx;                                            \
        GimbalStatus_t st{};                                  \
        if (!ReadStatus(&st)) return 0;                       \
        return static_cast<int32_t>(st.field);                \
    }

#define DP_GIMBAL_BOOL_READER(fn, field)                      \
    static bool fn(void* ctx) {                               \
        (void)ctx;                                            \
        GimbalStatus_t st{};                                  \
        if (!ReadStatus(&st)) return false;                   \
        return static_cast<bool>(st.field);                   \
    }

DP_GIMBAL_INT_READER(dp_gimbal_read_pan, pan)
DP_GIMBAL_INT_READER(dp_gimbal_read_tilt, tilt)
DP_GIMBAL_INT_READER(dp_gimbal_read_pan_speed, pan_speed)
DP_GIMBAL_INT_READER(dp_gimbal_read_tilt_speed, tilt_speed)
DP_GIMBAL_INT_READER(dp_gimbal_read_step_deg, step_deg)
DP_GIMBAL_INT_READER(dp_gimbal_read_lim_pan_min, lim_pan_min)
DP_GIMBAL_INT_READER(dp_gimbal_read_lim_pan_max, lim_pan_max)
DP_GIMBAL_INT_READER(dp_gimbal_read_lim_tilt_min, lim_tilt_min)
DP_GIMBAL_INT_READER(dp_gimbal_read_lim_tilt_max, lim_tilt_max)
DP_GIMBAL_BOOL_READER(dp_gimbal_read_moving_pan, moving_pan)
DP_GIMBAL_BOOL_READER(dp_gimbal_read_moving_tilt, moving_tilt)
DP_GIMBAL_BOOL_READER(dp_gimbal_read_ready, ready)

// ============================================================
// 数据点定义表（12 个，全部只读）
// ============================================================

#define DP_GIMBAL_INT_POINT(id, name, desc, reader)                    \
    {                                                                  \
        id, name, "gimbal", nullptr, desc,                             \
        DP_TYPE_INT, "°", 0,                                           \
        -1000000.0f, 1000000.0f, 1,                                    \
        nullptr,                                                       \
        DP_ACCESS_RO,                                                  \
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,                           \
        DP_POLICY_ON_CHANGE, 0, 0,                                     \
        nullptr, nullptr,                                              \
        { .read_int = reader }, { nullptr },                           \
    }

#define DP_GIMBAL_BOOL_POINT(id, name, desc, reader)                   \
    {                                                                  \
        id, name, "gimbal", nullptr, desc,                             \
        DP_TYPE_BOOL, "", 0,                                           \
        0, 0, 0,                                                       \
        nullptr,                                                       \
        DP_ACCESS_RO,                                                  \
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,                           \
        DP_POLICY_ON_CHANGE, 0, 0,                                     \
        nullptr, nullptr,                                              \
        { .read_bool = reader }, { nullptr },                          \
    }

static const DataPointDef kGimbalDataPoints[kGimbalDataPointCount] = {
    DP_GIMBAL_BOOL_POINT("gimbal.ready", "就绪", "云台是否就绪", dp_gimbal_read_ready),
    DP_GIMBAL_INT_POINT("gimbal.pan", "水平角", "水平（pan）角度", dp_gimbal_read_pan),
    DP_GIMBAL_INT_POINT("gimbal.tilt", "俯仰角", "俯仰（tilt）角度", dp_gimbal_read_tilt),
    DP_GIMBAL_BOOL_POINT("gimbal.moving_pan", "水平运动中", "水平轴是否在运动", dp_gimbal_read_moving_pan),
    DP_GIMBAL_BOOL_POINT("gimbal.moving_tilt", "俯仰运动中", "俯仰轴是否在运动", dp_gimbal_read_moving_tilt),
    DP_GIMBAL_INT_POINT("gimbal.pan_speed", "水平速度", "水平轴速度", dp_gimbal_read_pan_speed),
    DP_GIMBAL_INT_POINT("gimbal.tilt_speed", "俯仰速度", "俯仰轴速度", dp_gimbal_read_tilt_speed),
    DP_GIMBAL_INT_POINT("gimbal.step_deg", "步进角度", "单步角度", dp_gimbal_read_step_deg),
    DP_GIMBAL_INT_POINT("gimbal.lim_pan_min", "水平下限", "水平角下限", dp_gimbal_read_lim_pan_min),
    DP_GIMBAL_INT_POINT("gimbal.lim_pan_max", "水平上限", "水平角上限", dp_gimbal_read_lim_pan_max),
    DP_GIMBAL_INT_POINT("gimbal.lim_tilt_min", "俯仰下限", "俯仰角下限", dp_gimbal_read_lim_tilt_min),
    DP_GIMBAL_INT_POINT("gimbal.lim_tilt_max", "俯仰上限", "俯仰角上限", dp_gimbal_read_lim_tilt_max),
};

// ============================================================
// 模块定义
// ============================================================

static const DataModuleDef kGimbalModuleDef = {
    "gimbal",
    "云台",
    "云台角度、速度与限位状态",
    kGimbalDataPoints,
    kGimbalDataPointCount,
    false,
};

// ============================================================
// 公共 API
// ============================================================

void GimbalDataPoints_Register() {
    auto& reg = DataPointRegistry::Instance();
    bool ok = reg.RegisterModule(kGimbalModuleDef);
    ESP_LOGI(TAG, "register gimbal data points: ok=%d count=%u", ok ? 1 : 0,
             (unsigned)kGimbalDataPointCount);
}

const DataModuleDef* GimbalDataPoints_GetModuleDef() {
    return &kGimbalModuleDef;
}
