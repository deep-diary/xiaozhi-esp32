#include "mqtt/modules/motor_datapoints.h"

#include "config.h"  // 提供 board_features.h（DEEP_DOG_MOTOR_ENABLE）
#include "motor/motor_config.h"
#if DEEP_DOG_MOTOR_ENABLE
#include "motor/deep_motor.h"
#include "motor/motor_access.h"
#endif

#include <esp_log.h>

#include <climits>
#include <cstdio>
#include <cstdlib>

#define TAG "motor_dp"

namespace {

#if DEEP_DOG_MOTOR_ENABLE

int CtxToMotorId(void* ctx) {
    if (!ctx) return -1;
    return atoi(static_cast<const char*>(ctx));
}

// 取值来源：DeepDogMotorGet()->getMotorStatus(mid, ...)
bool ReadStatus(void* ctx, motor_status_t* out, DeepMotor** motor_out) {
    const int mid = CtxToMotorId(ctx);
    if (mid < 0) return false;
    DeepMotor* m = DeepDogMotorGet();
    if (!m) return false;
    if (!m->getMotorStatus(static_cast<uint8_t>(mid), out)) return false;
    if (motor_out) *motor_out = m;
    return true;
}

float ReadPositionRad(void* ctx) {
    motor_status_t st{};
    return ReadStatus(ctx, &st, nullptr) ? st.current_angle : 0.0f;
}
float ReadSpeedRadS(void* ctx) {
    motor_status_t st{};
    return ReadStatus(ctx, &st, nullptr) ? st.current_speed : 0.0f;
}
float ReadTorqueNm(void* ctx) {
    motor_status_t st{};
    return ReadStatus(ctx, &st, nullptr) ? st.current_torque : 0.0f;
}
float ReadTemperature(void* ctx) {
    motor_status_t st{};
    return ReadStatus(ctx, &st, nullptr) ? st.current_temp : 0.0f;
}
float ReadMaxAbsTorque(void* ctx) {
    motor_status_t st{};
    return ReadStatus(ctx, &st, nullptr) ? st.max_abs_torque : 0.0f;
}
int32_t ReadErrorStatus(void* ctx) {
    motor_status_t st{};
    return ReadStatus(ctx, &st, nullptr) ? static_cast<int32_t>(st.error_status) : 0;
}
int32_t ReadFeedbackSeq(void* ctx) {
    motor_status_t st{};
    return ReadStatus(ctx, &st, nullptr) ? static_cast<int32_t>(st.feedback_seq) : 0;
}
bool ReadFault(void* ctx) {
    motor_status_t st{};
    return ReadStatus(ctx, &st, nullptr) ? (st.error_status != 0) : false;
}
bool ReadCollision(void* ctx) {
    motor_status_t st{};
    return ReadStatus(ctx, &st, nullptr) ? st.collision : false;
}
bool ReadHasFeedback(void* ctx) {
    motor_status_t st{};
    return ReadStatus(ctx, &st, nullptr) ? st.has_feedback : false;
}
bool ReadMotorEnabled(void* ctx) {
    motor_status_t st{};
    return ReadStatus(ctx, &st, nullptr) ? st.motor_enabled : false;
}
float ReadTargetRad(void* ctx) {
    const int mid = CtxToMotorId(ctx);
    DeepMotor* m = nullptr;
    motor_status_t st{};
    if (!ReadStatus(ctx, &st, &m) || !m || mid < 0) return 0.0f;
    float t = 0.0f;
    return m->getMotorTargetAngle(static_cast<uint8_t>(mid), &t) ? t : 0.0f;
}

#endif  // DEEP_DOG_MOTOR_ENABLE

}  // namespace

// ============================================================
// 模板点（id 用 module.point_name，注册时按电机 CAN id 展开）
// ============================================================

#define DP_MOTOR_FLOAT_POINT(id, name, desc, reader, unit, pol)        \
    {                                                                  \
        id, name, "motor", nullptr, desc,                              \
        DP_TYPE_FLOAT, unit, 4,                                        \
        -100000.0f, 100000.0f, 0.0f,                                   \
        nullptr,                                                       \
        DP_ACCESS_RO,                                                  \
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP | DP_CH_TELEMETRY,         \
        pol, 100, 0,                                                   \
        nullptr, nullptr,                                              \
        { .read_float = reader }, { nullptr },                         \
    }

#define DP_MOTOR_INT_POINT(id, name, desc, reader)                     \
    {                                                                  \
        id, name, "motor", nullptr, desc,                              \
        DP_TYPE_INT, "", 0, 0.0f, 2147483647.0f, 1,                    \
        nullptr,                                                       \
        DP_ACCESS_RO,                                                  \
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,                           \
        DP_POLICY_ON_CHANGE, 0, 0,                                     \
        nullptr, nullptr,                                              \
        { .read_int = reader }, { nullptr },                           \
    }

#define DP_MOTOR_BOOL_POINT(id, name, desc, reader)                    \
    {                                                                  \
        id, name, "motor", nullptr, desc,                              \
        DP_TYPE_BOOL, "", 0, 0, 0, 0,                                  \
        nullptr,                                                       \
        DP_ACCESS_RO,                                                  \
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,                           \
        DP_POLICY_ON_CHANGE, 0, 0,                                     \
        nullptr, nullptr,                                              \
        { .read_bool = reader }, { nullptr },                          \
    }

#if DEEP_DOG_MOTOR_ENABLE

static const DataPointDef kMotorTemplatePoints[kMotorPointPerInstance] = {
    DP_MOTOR_FLOAT_POINT("motor.position_rad", "位置", "电机当前位置（弧度）", ReadPositionRad, "rad", DP_POLICY_PERIODIC),
    DP_MOTOR_FLOAT_POINT("motor.speed_rad_s", "速度", "电机当前速度（弧度/秒）", ReadSpeedRadS, "rad/s", DP_POLICY_PERIODIC),
    DP_MOTOR_FLOAT_POINT("motor.torque_nm", "力矩", "电机当前力矩（N·m）", ReadTorqueNm, "N·m", DP_POLICY_PERIODIC),
    DP_MOTOR_FLOAT_POINT("motor.temperature", "温度", "电机温度（℃）", ReadTemperature, "°C", DP_POLICY_PERIODIC),
    DP_MOTOR_FLOAT_POINT("motor.max_abs_torque", "最大力矩", "最大绝对力矩（N·m）", ReadMaxAbsTorque, "N·m", DP_POLICY_RETAIN),
    DP_MOTOR_FLOAT_POINT("motor.target_rad", "目标位置", "电机目标位置（弧度）", ReadTargetRad, "rad", DP_POLICY_ON_CHANGE),
    DP_MOTOR_INT_POINT("motor.error_status", "错误码", "电机错误状态码", ReadErrorStatus),
    DP_MOTOR_INT_POINT("motor.feedback_seq", "反馈序号", "反馈帧序号", ReadFeedbackSeq),
    DP_MOTOR_BOOL_POINT("motor.fault", "故障", "电机是否故障", ReadFault),
    DP_MOTOR_BOOL_POINT("motor.collision", "碰撞", "电机是否碰撞", ReadCollision),
    DP_MOTOR_BOOL_POINT("motor.has_feedback", "有反馈", "是否有反馈", ReadHasFeedback),
    DP_MOTOR_BOOL_POINT("motor.enabled", "已使能", "电机是否使能", ReadMotorEnabled),
};

static const DataModuleDef kMotorModuleTemplate = {
    "motor",
    "电机",
    "CAN 电机位置/速度/力矩/温度（多实例）",
    kMotorTemplatePoints,
    kMotorPointPerInstance,
    true,  // has_sub_instances
};

#else

static const DataPointDef kMotorTemplatePoints[1] = {};
static const DataModuleDef kMotorModuleTemplate = {"motor", "电机", "电机（未启用）", kMotorTemplatePoints, 0, true};

#endif

// ============================================================
// 公共 API
// ============================================================

bool MotorDataPoints_Register() {
#if DEEP_DOG_MOTOR_ENABLE
    static bool s_warned = false;
    DeepMotor* m = DeepDogMotorGet();
    if (!m) {
        if (!s_warned) {
            s_warned = true;
            ESP_LOGW(TAG, "DeepMotor not ready, defer motor data points");
        }
        return false;  // 电机未就绪
    }

    int8_t ids[MAX_MOTOR_COUNT];
    int n = static_cast<int>(m->getRegisteredMotorIds(ids, MAX_MOTOR_COUNT));
    if (n <= 0) {
        if (!s_warned) {
            s_warned = true;
            ESP_LOGW(TAG, "no CAN motor discovered yet, defer motor data points");
        }
        return false;  // 尚未扫描到电机，稍后重试
    }

    // sub_id = 电机 CAN id；registry 会拷贝持久化，局部数组安全
    static char id_buf[MAX_MOTOR_COUNT][8];
    const char* sub_ids[MAX_MOTOR_COUNT];
    const int cap = (n > MAX_MOTOR_COUNT) ? MAX_MOTOR_COUNT : n;
    for (int i = 0; i < cap; ++i) {
        snprintf(id_buf[i], sizeof(id_buf[i]), "%d", static_cast<int>(ids[i]));
        sub_ids[i] = id_buf[i];
    }

    auto& reg = DataPointRegistry::Instance();
    bool ok = reg.RegisterMultiInstanceModule(kMotorModuleTemplate, sub_ids, static_cast<size_t>(cap));
    ESP_LOGI(TAG, "register motor data points: ok=%d sub=%d x %u points", ok ? 1 : 0, cap,
             (unsigned)kMotorPointPerInstance);
    return ok;
#else
    return false;
#endif
}

const DataModuleDef* MotorDataPoints_GetModuleDef() {
    return &kMotorModuleTemplate;
}
