#include "mqtt/modules/touch_datapoints.h"

#include "touch_btn/touch_event_hub.h"
#include "touch_btn/touch_button_controller.h"

#include <esp_log.h>

#include <climits>

#define TAG "touch_dp"

namespace {

TouchEventHub* s_hub = nullptr;
TouchButtonController* s_ctrl = nullptr;

// TouchButtonState 的完整定义来自 touch_button_controller.h / touch_event_hub.h
bool ReadBtnPressed(int index) {
    TouchButtonState st{};
    if (s_hub && s_hub->GetButtonState(index, &st)) {
        return st.pressed;
    }
    if (s_ctrl && s_ctrl->GetButtonState(index, &st)) {
        return st.pressed;
    }
    return false;
}

bool ReadOk(void* ctx) {
    (void)ctx;
    return (s_hub != nullptr) || (s_ctrl != nullptr);
}

int32_t ReadPressedMask(void* ctx) {
    (void)ctx;
    if (s_hub) return static_cast<int32_t>(s_hub->GetPressedMask());
    if (s_ctrl) return static_cast<int32_t>(s_ctrl->GetPressedMask());
    return 0;
}

bool ReadBtn1(void* ctx) { (void)ctx; return ReadBtnPressed(1); }
bool ReadBtn2(void* ctx) { (void)ctx; return ReadBtnPressed(2); }
bool ReadBtn3(void* ctx) { (void)ctx; return ReadBtnPressed(3); }

}  // namespace

#define DP_TOUCH_BOOL_POINT(id, name, desc, reader)                    \
    {                                                                  \
        id, name, "touch", nullptr, desc,                              \
        DP_TYPE_BOOL, "", 0, 0, 0, 0,                                  \
        nullptr,                                                       \
        DP_ACCESS_RO,                                                  \
        DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,                           \
        DP_POLICY_ON_CHANGE, 0, 0,                                     \
        nullptr, nullptr,                                              \
        { .read_bool = reader }, { nullptr },                          \
    }

static const DataPointDef kTouchDataPoints[kTouchDataPointCount] = {
    {
        "touch.ok", "可用", "touch", nullptr, "触摸控制器是否可用",
        DP_TYPE_BOOL, "", 0, 0, 0, 0,
        nullptr,
        DP_ACCESS_RO, DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,
        DP_POLICY_ON_CHANGE, 0, 0,
        nullptr, nullptr,
        { .read_bool = ReadOk }, { nullptr },
    },
    {
        "touch.pressed_mask", "按下掩码", "touch", nullptr, "当前按下按钮位掩码",
        DP_TYPE_INT, "", 0, 0.0f, 255.0f, 1,
        nullptr,
        DP_ACCESS_RO, DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP,
        DP_POLICY_ON_CHANGE, 0, 0,
        nullptr, nullptr,
        { .read_int = ReadPressedMask }, { nullptr },
    },
    DP_TOUCH_BOOL_POINT("touch.btn1_pressed", "按钮1", "触摸按钮1是否按下", ReadBtn1),
    DP_TOUCH_BOOL_POINT("touch.btn2_pressed", "按钮2", "触摸按钮2是否按下", ReadBtn2),
    DP_TOUCH_BOOL_POINT("touch.btn3_pressed", "按钮3", "触摸按钮3是否按下", ReadBtn3),
};

static const DataModuleDef kTouchModuleDef = {
    "touch",
    "触摸按键",
    "触摸按钮状态",
    kTouchDataPoints,
    kTouchDataPointCount,
    false,
};

void TouchDataPoints_Register(TouchEventHub* hub, TouchButtonController* ctrl) {
    s_hub = hub;
    s_ctrl = ctrl;
    auto& reg = DataPointRegistry::Instance();
    bool ok = reg.RegisterModule(kTouchModuleDef);
    ESP_LOGI(TAG, "register touch data points: ok=%d count=%u", ok ? 1 : 0,
             (unsigned)kTouchDataPointCount);
}

const DataModuleDef* TouchDataPoints_GetModuleDef() {
    return &kTouchModuleDef;
}
