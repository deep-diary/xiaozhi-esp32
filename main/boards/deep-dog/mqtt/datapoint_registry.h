#pragma once

/**
 * Data Point Registry —— 声明式数据点目录 + 统一发布引擎
 *
 * 设计目标：
 *   每个数据点只声明一次，三通道（MQTT / HTTP / MCP / Telemetry）自动生效。
 *   取代各模块中散落的 cJSON_Add*ToObject / snprintf / MCP RegisterTool 调用。
 *
 * 范围：
 *   覆盖「标量可观测/可控制量」（bool / int / float / string / enum）。
 *   复杂过程性操作（如 motor.scan、face.recognize）仍走独立 MCP tool。
 *
 *   文档：docs/interface/04-datapoint-registry.md
 */

#include <cstdint>
#include <cstddef>
#include <string>

struct cJSON;
class DeepDogMqttClient;

// ============================================================
// 枚举定义
// ============================================================

/** 数据点类型 */
enum DpType : uint8_t {
    DP_TYPE_BOOL   = 0,
    DP_TYPE_INT    = 1,   // int32
    DP_TYPE_FLOAT  = 2,
    DP_TYPE_STRING = 3,
    DP_TYPE_ENUM   = 4,   // 语义为 string，带枚举值约束
};

/** 访问模式 */
enum DpAccess : uint8_t {
    DP_ACCESS_RO = 0,  // 只读上报
    DP_ACCESS_WO = 1,  // 只写指令
    DP_ACCESS_RW = 2,  // 可读可写
};

/** 发布策略 */
enum DpPolicy : uint8_t {
    DP_POLICY_RETAIN     = 0,  // retain 发布，值变化时更新（默认）
    DP_POLICY_ON_CHANGE  = 1,  // 仅变化时发布，retain=true
    DP_POLICY_PERIODIC   = 2,  // 周期发布，retain=false
    DP_POLICY_ON_DEMAND  = 3,  // 按需发布（显式触发），retain=false
};

/** 通道位掩码（可组合） */
enum DpChannel : uint8_t {
    DP_CH_MQTT      = 1 << 0,   // 0b0001 — MQTT status/cmd
    DP_CH_HTTP      = 1 << 1,   // 0b0010 — HTTP REST
    DP_CH_MCP       = 1 << 2,   // 0b0100 — MCP tools (get_/set_)
    DP_CH_TELEMETRY = 1 << 3,   // 0b1000 — 自动汇入 status/telemetry
};

/** 校验错误码 */
enum DpError : int8_t {
    DP_ERR_OK            = 0,
    DP_ERR_UNKNOWN_POINT = -1,
    DP_ERR_READ_ONLY     = -2,
    DP_ERR_WRITE_ONLY    = -3,
    DP_ERR_TYPE_MISMATCH = -4,
    DP_ERR_INVALID_ENUM  = -5,
    DP_ERR_OUT_OF_RANGE  = -6,
    DP_ERR_WRITE_FAILED  = -7,
    DP_ERR_NOT_SUPPORTED = -8,
};

// ============================================================
// 枚举值条目（用于 DP_TYPE_ENUM）
// ============================================================

struct DpEnumEntry {
    const char* value;   // 枚举值字符串
    const char* label;   // 可读名称（可为空）
};

// ============================================================
// 值容器（运行时读写用）
// ============================================================

union DpValue {
    bool     b;
    int32_t  i;
    float    f;
    const char* s;  // string/enum：调用方需确保生命周期
};

// ============================================================
// 读写回调类型
// ============================================================

using DpReadBoolCb   = bool (*)(void* ctx);
using DpReadIntCb    = int32_t (*)(void* ctx);
using DpReadFloatCb  = float (*)(void* ctx);
using DpReadStringCb = const char* (*)(void* ctx);

using DpWriteBoolCb   = bool (*)(void* ctx, bool value);         // 返回是否成功
using DpWriteIntCb    = bool (*)(void* ctx, int32_t value);
using DpWriteFloatCb  = bool (*)(void* ctx, float value);
using DpWriteStringCb = bool (*)(void* ctx, const char* value);

// ============================================================
// 数据点定义（静态声明表用）
// ============================================================

struct DataPointDef {
    /** 数据点ID：module[.sub_id].point_name，如 "motor.0.position_rad" */
    const char* id;

    /** 可读名称，用于前端标签 */
    const char* name;

    /** 所属模块名（MQTT subject 第一级） */
    const char* module;

    /** 子实例ID（如电机号），无则为 nullptr */
    const char* sub_id;

    /** 详细描述 */
    const char* description;

    /** 数据类型 */
    DpType type;

    /** 单位（空字符串表示无量纲） */
    const char* unit;

    /** 小数精度（float 型），0 表示整数精度 */
    uint8_t precision;

    /** 下限（int/float），NaN 或特殊值表示负无穷；int 用 INT32_MIN 表示无下限 */
    float min_val;

    /** 上限（int/float），NaN 或特殊值表示正无穷；int 用 INT32_MAX 表示无上限 */
    float max_val;

    /** 步长/分辨率（0 表示不限制） */
    float step;

    /** 枚举值数组（type=DP_TYPE_ENUM 时有效），以 {nullptr,nullptr} 结尾 */
    const DpEnumEntry* enum_values;

    /** 访问模式 */
    DpAccess access;

    /** 生效通道（位掩码） */
    uint8_t channels;

    /** 发布策略 */
    DpPolicy policy;

    /** 周期发布间隔（ms），policy=periodic 时有效 */
    uint32_t period_ms;

    /** 死区阈值（on_change 策略，数值型），变化超过此值才发布 */
    float deadband;

    /** 读回调上下文（透传给 read_cb） */
    void* read_ctx;

    /** 写回调上下文（透传给 write_cb） */
    void* write_ctx;

    // --- 读写回调（按类型选择） ---
    union {
        DpReadBoolCb   read_bool;
        DpReadIntCb    read_int;
        DpReadFloatCb  read_float;
        DpReadStringCb read_string;
    };

    union {
        DpWriteBoolCb   write_bool;
        DpWriteIntCb    write_int;
        DpWriteFloatCb  write_float;
        DpWriteStringCb write_string;
    };
};

// ============================================================
// 模块定义
// ============================================================

struct DataModuleDef {
    const char* name;            // 模块名，如 "led"、"motor"
    const char* display_name;    // 中文显示名
    const char* description;     // 模块描述

    const DataPointDef* points;  // 数据点数组
    size_t point_count;          // 数据点数量

    /** 是否有子实例（如 motor 有多个电机） */
    bool has_sub_instances;
};

// ============================================================
// DataPointRegistry —— 全局单例
// ============================================================

class DataPointRegistry {
public:
    static DataPointRegistry& Instance();

    DataPointRegistry();
    ~DataPointRegistry();

    // ---- 模块注册 ----

    /** 注册一个模块（无子实例） */
    bool RegisterModule(const DataModuleDef& module_def);

    /** 注册有子实例的模块（instances 由子实例数量展开） */
    bool RegisterMultiInstanceModule(const DataModuleDef& module_template,
                                     const char** sub_ids, size_t sub_count);

    // ---- 查询 ----

    /** 根据 id 查找数据点定义；返回 nullptr 表示未找到 */
    const DataPointDef* FindPoint(const char* point_id) const;

    /** 获取模块数量 */
    size_t ModuleCount() const;

    /** 获取第 idx 个模块定义 */
    const DataModuleDef* GetModule(size_t idx) const;

    /** 根据模块名查找模块定义 */
    const DataModuleDef* FindModule(const char* module_name) const;

    // ---- 读取 ----

    /** 读取一个数据点的当前值（存入 DpValue）；返回错误码 */
    DpError ReadValue(const char* point_id, DpValue& out_value) const;

    // ---- 写入 ----

    /** 写入一个数据点的值；返回错误码 */
    DpError WriteValue(const char* point_id, const DpValue& value);

    // ---- 序列化 ----

    /** 将指定模块的所有 RO/RW 数据点序列化为 JSON 对象（追加到 target_obj） */
    bool SerializeModuleStatus(const char* module_name, cJSON* target_obj) const;

    /** 生成完整 schema JSON（用于 info/schema 主题发布） */
    cJSON* BuildSchemaJson() const;

    // ---- 触发发布 ----

    /** 标记某数据点已变化，触发发布（按策略判断是否真正发布） */
    void MarkChanged(const char* point_id);

    /** 强制发布指定模块的 status */
    bool ForcePublishModule(const char* module_name);

    // ---- MQTT 对接 ----

    /** 设置 MQTT 客户端（由 deep_dog_mqtt 调用） */
    void SetMqttClient(DeepDogMqttClient* client);

    /** MQTT 连接建立后调用：发布所有 retain 状态 + schema */
    void OnMqttConnected();

    /** MQTT 断开后调用 */
    void OnMqttDisconnected();

    /** 处理来自 cmd/<module> 的下行消息 */
    void HandleCmdMessage(const std::string& module_name, const std::string& payload);

private:
    struct Impl;
    Impl* impl_;

    bool mqtt_connected_ = false;
    DeepDogMqttClient* mqtt_client_ = nullptr;
};
