#include "mqtt/modules/can_datapoints.h"

#include "can/can_config.h"
#include "can/ESP32-TWAI-CAN.hpp"

#include <esp_log.h>

#include <climits>

#define TAG "can_dp"

// ============================================================
// 上下文（由 DeepDogCanMqtt 提供）
// ============================================================

namespace {

bool* s_tunnel = nullptr;
bool* s_allow_tx = nullptr;
const uint32_t* s_dropped = nullptr;

bool ReadOk(void* ctx) { (void)ctx; return true; }
bool ReadTunnel(void* ctx) { (void)ctx; return s_tunnel ? *s_tunnel : false; }
bool ReadAllowTx(void* ctx) { (void)ctx; return s_allow_tx ? *s_allow_tx : false; }
int32_t ReadBitrate(void* ctx) { (void)ctx; return 1000; }
int32_t ReadTxGpio(void* ctx) { (void)ctx; return static_cast<int32_t>(CAN_TX_GPIO); }
int32_t ReadRxGpio(void* ctx) { (void)ctx; return static_cast<int32_t>(CAN_RX_GPIO); }
int32_t ReadBusState(void* ctx) { (void)ctx; return static_cast<int32_t>(ESP32Can.canState()); }
int32_t ReadRxErr(void* ctx) { (void)ctx; return static_cast<int32_t>(ESP32Can.rxErrorCounter()); }
int32_t ReadTxErr(void* ctx) { (void)ctx; return static_cast<int32_t>(ESP32Can.txErrorCounter()); }
int32_t ReadBusErr(void* ctx) { (void)ctx; return static_cast<int32_t>(ESP32Can.busErrCounter()); }
int32_t ReadDropped(void* ctx) { (void)ctx; return s_dropped ? static_cast<int32_t>(*s_dropped) : 0; }

// ---- 写回调（Phase D：数据点写入，经 registry 统一校验） ----
bool WriteTunnel(void* ctx, bool v) {
    (void)ctx;
    if (!s_tunnel) return false;
    *s_tunnel = v;
    return true;
}

bool WriteAllowTx(void* ctx, bool v) {
    (void)ctx;
    if (!s_allow_tx) return false;
    if (v) {
        ESP_LOGW(TAG, "can allow_tx ENABLED via data point — web may inject frames");
    }
    *s_allow_tx = v;
    return true;
}

}  // namespace

#define DP_CAN_BOOL_POINT(id, name, desc, reader)                      \
    {                                                                  \
        id, name, "can", nullptr, desc,                                \
        DP_TYPE_BOOL, "", 0, 0, 0, 0,                                  \
        nullptr,                                                       \
        DP_ACCESS_RO,                                                  \
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,                           \
        DP_POLICY_ON_CHANGE, 0, 0,                                     \
        nullptr, nullptr,                                              \
        { .read_bool = reader }, { nullptr },                          \
    }

#define DP_CAN_BOOL_RW_POINT(id, name, desc, reader, writer)           \
    {                                                                  \
        id, name, "can", nullptr, desc,                                \
        DP_TYPE_BOOL, "", 0, 0, 0, 0,                                  \
        nullptr,                                                       \
        DP_ACCESS_RW,                                                  \
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,                           \
        DP_POLICY_ON_CHANGE, 0, 0,                                     \
        nullptr, nullptr,                                              \
        { .read_bool = reader }, { .write_bool = writer },             \
    }

#define DP_CAN_INT_POINT(id, name, desc, reader, pmin, pmax, pol)      \
    {                                                                  \
        id, name, "can", nullptr, desc,                                \
        DP_TYPE_INT, "", 0, pmin, pmax, 1,                             \
        nullptr,                                                       \
        DP_ACCESS_RO,                                                  \
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,                           \
        pol, 0, 0,                                                     \
        nullptr, nullptr,                                              \
        { .read_int = reader }, { nullptr },                           \
    }

static const DataPointDef kCanDataPoints[kCanDataPointCount] = {
    DP_CAN_BOOL_POINT("can.ok", "可用", "CAN 总线是否可用", ReadOk),
    DP_CAN_BOOL_RW_POINT("can.tunnel", "隧道模式", "CAN 帧隧道是否开启（可写）", ReadTunnel, WriteTunnel),
    DP_CAN_BOOL_RW_POINT("can.allow_tx", "允许发送", "是否允许网页注入帧（可写）", ReadAllowTx, WriteAllowTx),
    DP_CAN_INT_POINT("can.bitrate_kbps", "波特率", "CAN 波特率", ReadBitrate, 0.0f, 10000.0f, DP_POLICY_RETAIN),
    DP_CAN_INT_POINT("can.tx_gpio", "TX 引脚", "CAN TX GPIO", ReadTxGpio, 0.0f, 64.0f, DP_POLICY_RETAIN),
    DP_CAN_INT_POINT("can.rx_gpio", "RX 引脚", "CAN RX GPIO", ReadRxGpio, 0.0f, 64.0f, DP_POLICY_RETAIN),
    DP_CAN_INT_POINT("can.bus_state", "总线状态", "CAN 控制器状态", ReadBusState, 0.0f, 255.0f, DP_POLICY_ON_CHANGE),
    DP_CAN_INT_POINT("can.rx_err", "接收错误计数", "CAN 接收错误计数", ReadRxErr, 0.0f, 255.0f, DP_POLICY_PERIODIC),
    DP_CAN_INT_POINT("can.tx_err", "发送错误计数", "CAN 发送错误计数", ReadTxErr, 0.0f, 255.0f, DP_POLICY_PERIODIC),
    DP_CAN_INT_POINT("can.bus_err", "总线错误计数", "CAN 总线错误计数", ReadBusErr, 0.0f, 65535.0f, DP_POLICY_PERIODIC),
    DP_CAN_INT_POINT("can.dropped", "丢弃帧数", "因限流丢弃的帧数", ReadDropped, 0.0f, 2147483647.0f, DP_POLICY_PERIODIC),
};

static const DataModuleDef kCanModuleDef = {
    "can",
    "CAN 总线",
    "CAN 控制器状态与错误计数",
    kCanDataPoints,
    kCanDataPointCount,
    false,
};

void CanDataPoints_Register(bool* tunnel, bool* allow_tx, const uint32_t* dropped) {
    s_tunnel = tunnel;
    s_allow_tx = allow_tx;
    s_dropped = dropped;
    auto& reg = DataPointRegistry::Instance();
    bool ok = reg.RegisterModule(kCanModuleDef);
    ESP_LOGI(TAG, "register can data points: ok=%d count=%u", ok ? 1 : 0,
             (unsigned)kCanDataPointCount);
}

const DataModuleDef* CanDataPoints_GetModuleDef() {
    return &kCanModuleDef;
}
