#include "handle/sources/handle_bt.h"

#include "handle/handle_event_hub.h"

#include <esp_log.h>

#define TAG "handle_bt"

#if !DEEP_DOG_HANDLE_BT_ENABLE

bool HandleBtStart(HandleEventHub* hub) {
    (void)hub;
    ESP_LOGI(TAG, "BT source disabled (DEEP_DOG_HANDLE_BT_ENABLE=0)");
    return false;
}

void HandleBtStop() {}

void HandleBtStartPairing() {
    ESP_LOGW(TAG, "pair ignored (HANDLE_BT disabled)");
}

void HandleBtRumble(uint16_t delay_ms, uint16_t duration_ms, uint8_t weak, uint8_t strong) {
    (void)delay_ms;
    (void)duration_ms;
    (void)weak;
    (void)strong;
    ESP_LOGW(TAG, "rumble ignored (HANDLE_BT disabled)");
}

bool HandleBtIsReady() {
    return false;
}

#else  // DEEP_DOG_HANDLE_BT_ENABLE

#include <cstring>

#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <btstack_port_esp32.h>
#include <btstack_run_loop.h>
#include <uni.h>

#ifndef CONFIG_BLUEPAD32_PLATFORM_CUSTOM
#error "Bluepad32 must be built with CONFIG_BLUEPAD32_PLATFORM_CUSTOM"
#endif

namespace {

HandleEventHub* g_hub = nullptr;
uni_hid_device_t* g_device = nullptr;
bool g_ready = false;
bool g_started = false;

constexpr float kAxisHalf = 512.f;
constexpr float kTriggerMax = 1023.f;

float NormAxis(int32_t v) {
    float f = static_cast<float>(v) / kAxisHalf;
    if (f < -1.f) {
        return -1.f;
    }
    if (f > 1.f) {
        return 1.f;
    }
    return f;
}

float NormTrigger(int32_t v) {
    if (v <= 0) {
        return 0.f;
    }
    float f = static_cast<float>(v) / kTriggerMax;
    return f > 1.f ? 1.f : f;
}

int64_t NowUs() {
    return static_cast<int64_t>(esp_timer_get_time());
}

HandleSnapshot Normalize(const uni_gamepad_t& gp, bool connected) {
    HandleSnapshot snap{};
    snap.connected = connected;
    snap.source = HandleSource::kBt;
    snap.ts_us = NowUs();
    if (!connected) {
        return snap;
    }

    // Bluepad32: axis_y 上推为负、下推为正，与 I01「右/下为正」一致；勿取反。
    // 前端若圆点上下反，改显示层（stickStyle），不要改本归一化。
    snap.axes.lx = NormAxis(gp.axis_x);
    snap.axes.ly = NormAxis(gp.axis_y);
    snap.axes.rx = NormAxis(gp.axis_rx);
    snap.axes.ry = NormAxis(gp.axis_ry);

    snap.buttons.a = (gp.buttons & BUTTON_A) != 0;
    snap.buttons.b = (gp.buttons & BUTTON_B) != 0;
    snap.buttons.x = (gp.buttons & BUTTON_X) != 0;
    snap.buttons.y = (gp.buttons & BUTTON_Y) != 0;
    snap.buttons.l1 = (gp.buttons & BUTTON_SHOULDER_L) != 0;
    snap.buttons.r1 = (gp.buttons & BUTTON_SHOULDER_R) != 0;
    snap.buttons.l3 = (gp.buttons & BUTTON_THUMB_L) != 0;
    snap.buttons.r3 = (gp.buttons & BUTTON_THUMB_R) != 0;
    snap.buttons.l2 = NormTrigger(gp.brake);
    snap.buttons.r2 = NormTrigger(gp.throttle);
    snap.buttons.select = (gp.misc_buttons & MISC_BUTTON_SELECT) != 0;
    snap.buttons.start = (gp.misc_buttons & MISC_BUTTON_START) != 0;
    snap.buttons.ps = (gp.misc_buttons & MISC_BUTTON_SYSTEM) != 0;
    snap.buttons.dpad_up = (gp.dpad & DPAD_UP) != 0;
    snap.buttons.dpad_down = (gp.dpad & DPAD_DOWN) != 0;
    snap.buttons.dpad_left = (gp.dpad & DPAD_LEFT) != 0;
    snap.buttons.dpad_right = (gp.dpad & DPAD_RIGHT) != 0;
    return snap;
}

void PushSnap(const HandleSnapshot& snap) {
    if (g_hub) {
        g_hub->Push(snap);
    }
}

void PushDisconnected() {
    PushSnap(Normalize(uni_gamepad_t{}, false));
}

struct RumbleReq {
    uint16_t delay_ms;
    uint16_t duration_ms;
    uint8_t weak;
    uint8_t strong;
};

RumbleReq g_rumble_req{};
btstack_context_callback_registration_t g_rumble_cb{};

static void RumbleOnBtThread(void* context) {
    auto* req = static_cast<RumbleReq*>(context);
    if (!req) {
        return;
    }
    uni_hid_device_t* d = g_device;
    if (d && d->report_parser.play_dual_rumble) {
        d->report_parser.play_dual_rumble(d, req->delay_ms, req->duration_ms, req->weak, req->strong);
    } else {
        ESP_LOGW(TAG, "rumble: no device or unsupported");
    }
}

static void platform_init(int argc, const char** argv) {
    (void)argc;
    (void)argv;
    logi("deep-dog handle_bt: platform init\n");
}

static void platform_on_init_complete(void) {
    logi("deep-dog handle_bt: init complete, start scan\n");
    uni_bt_start_scanning_and_autoconnect_unsafe();
    uni_bt_allow_incoming_connections(true);
}

static uni_error_t platform_on_device_discovered(bd_addr_t addr, const char* name, uint16_t cod, uint8_t rssi) {
    (void)addr;
    (void)name;
    (void)rssi;
    if (((cod & UNI_BT_COD_MINOR_MASK) & UNI_BT_COD_MINOR_KEYBOARD) == UNI_BT_COD_MINOR_KEYBOARD) {
        return UNI_ERROR_IGNORE_DEVICE;
    }
    return UNI_ERROR_SUCCESS;
}

static void platform_on_device_connected(uni_hid_device_t* d) {
    logi("deep-dog handle_bt: connected %p\n", d);
}

static void platform_on_device_disconnected(uni_hid_device_t* d) {
    logi("deep-dog handle_bt: disconnected %p\n", d);
    if (d == g_device) {
        g_device = nullptr;
        g_ready = false;
        PushDisconnected();
    }
    // Xbox BLE 重连需要中央设备继续扫描
    uni_bt_start_scanning_and_autoconnect_safe();
    logi("deep-dog handle_bt: resume scan after disconnect\n");
}

static uni_error_t platform_on_device_ready(uni_hid_device_t* d) {
    logi("deep-dog handle_bt: ready %p\n", d);
    g_device = d;
    g_ready = true;
    // 已连接则停扫：扫描本身是固定开销（非无限涨堆），停扫可降功耗/调度
    uni_bt_stop_scanning_safe();
    if (d && d->report_parser.play_dual_rumble) {
        d->report_parser.play_dual_rumble(d, 0, 120, 100, 40);
    }
    return UNI_ERROR_SUCCESS;
}

static void platform_on_controller_data(uni_hid_device_t* d, uni_controller_t* ctl) {
    if (!ctl || ctl->klass != UNI_CONTROLLER_CLASS_GAMEPAD) {
        return;
    }
    g_device = d;
    PushSnap(Normalize(ctl->gamepad, true));
}

static const uni_property_t* platform_get_property(uni_property_idx_t idx) {
    (void)idx;
    return nullptr;
}

static void platform_on_oob_event(uni_platform_oob_event_t event, void* data) {
    (void)data;
    if (event == UNI_PLATFORM_OOB_BLUETOOTH_ENABLED) {
        logi("deep-dog handle_bt: bluetooth enabled event\n");
    }
}

struct uni_platform* get_deep_dog_platform(void) {
    // Field order must match uni_platform.h (C++ designated-init rule)
    static struct uni_platform plat = {
        .name = "deep-dog",
        .init = platform_init,
        .on_init_complete = platform_on_init_complete,
        .on_device_discovered = platform_on_device_discovered,
        .on_device_connected = platform_on_device_connected,
        .on_device_disconnected = platform_on_device_disconnected,
        .on_device_ready = platform_on_device_ready,
        .on_gamepad_data = nullptr,
        .on_controller_data = platform_on_controller_data,
        .get_property = platform_get_property,
        .on_oob_event = platform_on_oob_event,
        .device_dump = nullptr,
        .register_console_cmds = nullptr,
    };
    return &plat;
}

static void BtstackTask(void* /*arg*/) {
    btstack_init();
    uni_platform_set_custom(get_deep_dog_platform());
    uni_init(0, nullptr);
    btstack_run_loop_execute();
    vTaskDelete(nullptr);
}

}  // namespace

bool HandleBtStart(HandleEventHub* hub) {
    if (g_started) {
        return true;
    }
    g_hub = hub;
    // HCI/controller 必须 INTERNAL|DMA；先打快照便于对照 WiFi 后剩余
    const size_t dma_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    const size_t dma_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    const size_t int_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "pre-BT heap: internal=%u DMA_free=%u DMA_largest=%u", (unsigned)int_free,
             (unsigned)dma_free, (unsigned)dma_largest);
    // 栈必须在内部 RAM：BTstack/HCI 会触 flash（NVS/bond），PSRAM 栈会触发
    // esp_task_stack_is_sane_cache_disabled assert
    constexpr uint32_t kBtStackBytes = 6144;
    const BaseType_t ok =
        xTaskCreatePinnedToCore(BtstackTask, "bp32_bt", kBtStackBytes, nullptr, 5, nullptr, 0);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "failed to create BTstack task");
        return false;
    }
    g_started = true;
    ESP_LOGI(TAG, "Bluepad32/BTstack task started (scan after HCI up)");
    return true;
}

void HandleBtStop() {
    // BTstack run loop 通常不退出；仅停止扫描
    if (g_started) {
        uni_bt_stop_scanning_safe();
    }
}

void HandleBtStartPairing() {
    if (!g_started) {
        ESP_LOGW(TAG, "pair: BT not started");
        return;
    }
    uni_bt_start_scanning_and_autoconnect_safe();
    ESP_LOGI(TAG, "pairing: scanning");
}

void HandleBtRumble(uint16_t delay_ms, uint16_t duration_ms, uint8_t weak, uint8_t strong) {
    if (!g_started) {
        ESP_LOGW(TAG, "rumble: BT not started");
        return;
    }
    g_rumble_req = RumbleReq{delay_ms, duration_ms, weak, strong};
    g_rumble_cb.callback = &RumbleOnBtThread;
    g_rumble_cb.context = &g_rumble_req;
    btstack_run_loop_execute_on_main_thread(&g_rumble_cb);
}

bool HandleBtIsReady() {
    return g_ready && g_device != nullptr;
}

#endif  // DEEP_DOG_HANDLE_BT_ENABLE
