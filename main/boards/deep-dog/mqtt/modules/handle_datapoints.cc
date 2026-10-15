#include "mqtt/modules/handle_datapoints.h"

#include "handle/handle_event_hub.h"
#include "handle/handle_types.h"

#include <esp_log.h>

#include <climits>

#define TAG "handle_dp"

namespace {

HandleEventHub* s_hub = nullptr;

HandleSnapshot ReadSnap() {
    if (!s_hub) return HandleSnapshot{};
    return s_hub->GetSnapshot();
}

bool ReadConnected(void* ctx) { (void)ctx; return ReadSnap().connected; }
int32_t ReadSource(void* ctx) { (void)ctx; return static_cast<int32_t>(ReadSnap().source); }
float ReadAxesLx(void* ctx) { (void)ctx; return static_cast<float>(ReadSnap().axes.lx); }
float ReadAxesLy(void* ctx) { (void)ctx; return static_cast<float>(ReadSnap().axes.ly); }
float ReadAxesRx(void* ctx) { (void)ctx; return static_cast<float>(ReadSnap().axes.rx); }
float ReadAxesRy(void* ctx) { (void)ctx; return static_cast<float>(ReadSnap().axes.ry); }

bool ReadBtnA(void* ctx) { (void)ctx; return ReadSnap().buttons.a; }
bool ReadBtnB(void* ctx) { (void)ctx; return ReadSnap().buttons.b; }
bool ReadBtnX(void* ctx) { (void)ctx; return ReadSnap().buttons.x; }
bool ReadBtnY(void* ctx) { (void)ctx; return ReadSnap().buttons.y; }
bool ReadBtnL1(void* ctx) { (void)ctx; return ReadSnap().buttons.l1; }
bool ReadBtnR1(void* ctx) { (void)ctx; return ReadSnap().buttons.r1; }
bool ReadBtnStart(void* ctx) { (void)ctx; return ReadSnap().buttons.start; }
bool ReadBtnSelect(void* ctx) { (void)ctx; return ReadSnap().buttons.select; }

}  // namespace

#define DP_HANDLE_BOOL_POINT(id, name, desc, reader)                   \
    {                                                                  \
        id, name, "handle", nullptr, desc,                             \
        DP_TYPE_BOOL, "", 0, 0, 0, 0,                                  \
        nullptr,                                                       \
        DP_ACCESS_RO,                                                  \
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,                           \
        DP_POLICY_ON_CHANGE, 0, 0,                                     \
        nullptr, nullptr,                                              \
        { .read_bool = reader }, { nullptr },                          \
    }

#define DP_HANDLE_FLOAT_POINT(id, name, desc, reader)                  \
    {                                                                  \
        id, name, "handle", nullptr, desc,                             \
        DP_TYPE_FLOAT, "", 2,                                          \
        -1.0f, 1.0f, 0.01f,                                            \
        nullptr,                                                       \
        DP_ACCESS_RO,                                                  \
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,                           \
        DP_POLICY_ON_CHANGE, 0, 0,                                     \
        nullptr, nullptr,                                              \
        { .read_float = reader }, { nullptr },                         \
    }

static const DataPointDef kHandleDataPoints[kHandleDataPointCount] = {
    DP_HANDLE_BOOL_POINT("handle.connected", "已连接", "手柄是否已连接", ReadConnected),
    {
        "handle.source", "输入源", "handle", nullptr, "手柄输入源（枚举整数）",
        DP_TYPE_INT, "", 0, 0.0f, 16.0f, 1,
        nullptr,
        DP_ACCESS_RO, DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,
        DP_POLICY_ON_CHANGE, 0, 0,
        nullptr, nullptr,
        { .read_int = ReadSource }, { nullptr },
    },
    DP_HANDLE_FLOAT_POINT("handle.axes_lx", "左摇杆X", "左摇杆 X 轴", ReadAxesLx),
    DP_HANDLE_FLOAT_POINT("handle.axes_ly", "左摇杆Y", "左摇杆 Y 轴", ReadAxesLy),
    DP_HANDLE_FLOAT_POINT("handle.axes_rx", "右摇杆X", "右摇杆 X 轴", ReadAxesRx),
    DP_HANDLE_FLOAT_POINT("handle.axes_ry", "右摇杆Y", "右摇杆 Y 轴", ReadAxesRy),
    DP_HANDLE_BOOL_POINT("handle.btn_a", "A", "按键 A", ReadBtnA),
    DP_HANDLE_BOOL_POINT("handle.btn_b", "B", "按键 B", ReadBtnB),
    DP_HANDLE_BOOL_POINT("handle.btn_x", "X", "按键 X", ReadBtnX),
    DP_HANDLE_BOOL_POINT("handle.btn_y", "Y", "按键 Y", ReadBtnY),
    DP_HANDLE_BOOL_POINT("handle.btn_l1", "L1", "肩键 L1", ReadBtnL1),
    DP_HANDLE_BOOL_POINT("handle.btn_r1", "R1", "肩键 R1", ReadBtnR1),
    DP_HANDLE_BOOL_POINT("handle.btn_start", "Start", "按键 Start", ReadBtnStart),
    DP_HANDLE_BOOL_POINT("handle.btn_select", "Select", "按键 Select", ReadBtnSelect),
};

static const DataModuleDef kHandleModuleDef = {
    "handle",
    "手柄",
    "手柄连接、摇杆与按键状态",
    kHandleDataPoints,
    kHandleDataPointCount,
    false,
};

void HandleDataPoints_Register(HandleEventHub* hub) {
    s_hub = hub;
    auto& reg = DataPointRegistry::Instance();
    bool ok = reg.RegisterModule(kHandleModuleDef);
    ESP_LOGI(TAG, "register handle data points: ok=%d count=%u", ok ? 1 : 0,
             (unsigned)kHandleDataPointCount);
}

const DataModuleDef* HandleDataPoints_GetModuleDef() {
    return &kHandleModuleDef;
}
