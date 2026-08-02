#pragma once

#include <cstdint>

enum class HandleSource : uint8_t {
    kNone = 0,
    kBt = 1,
    kUsb = 2,
    kWifi = 3,
};

inline const char* HandleSourceName(HandleSource s) {
    switch (s) {
        case HandleSource::kBt:
            return "bt";
        case HandleSource::kUsb:
            return "usb";
        case HandleSource::kWifi:
            return "wifi";
        default:
            return nullptr;
    }
}

inline HandleSource HandleSourceFromName(const char* name) {
    if (!name) {
        return HandleSource::kNone;
    }
    if (name[0] == 'b' && name[1] == 't' && name[2] == '\0') {
        return HandleSource::kBt;
    }
    if (name[0] == 'u' && name[1] == 's' && name[2] == 'b' && name[3] == '\0') {
        return HandleSource::kUsb;
    }
    if (name[0] == 'w' && name[1] == 'i' && name[2] == 'f' && name[3] == 'i' && name[4] == '\0') {
        return HandleSource::kWifi;
    }
    return HandleSource::kNone;
}

struct HandleButtons {
    bool a = false;
    bool b = false;
    bool x = false;
    bool y = false;
    bool l1 = false;
    bool r1 = false;
    float l2 = 0.f;
    float r2 = 0.f;
    bool start = false;
    bool select = false;
    /** 可选扩展（PC 桥 / 前端示意；无则 false） */
    bool ps = false;
    bool l3 = false;
    bool r3 = false;
    bool touch = false;
    bool dpad_up = false;
    bool dpad_down = false;
    bool dpad_left = false;
    bool dpad_right = false;
};

struct HandleAxes {
    float lx = 0.f;
    float ly = 0.f;
    float rx = 0.f;
    float ry = 0.f;
};

/** 可选触控板坐标（I06）；present=false 表示 JSON 未带 touchpad */
struct HandleTouchContact {
    bool active = false;
    float x = 0.f;
    float y = 0.f;
};

struct HandleTouchpad {
    bool present = false;
    bool active = false;
    float x = 0.f;
    float y = 0.f;
    int fingers = 0;
    /** 0～2；contact_count=0 表示未带 contacts（仅用 x/y） */
    int contact_count = 0;
    HandleTouchContact contacts[2]{};
};

/** 可选手柄 IMU（I07）；present=false 表示 JSON 未带 motion */
struct HandleMotion {
    bool present = false;
    float gyro_x = 0.f;
    float gyro_y = 0.f;
    float gyro_z = 0.f;
    float accel_x = 0.f;
    float accel_y = 0.f;
    float accel_z = 0.f;
};

/** 与 YAML handle/status|input 对齐的控制快照 */
struct HandleSnapshot {
    bool connected = false;
    HandleSource source = HandleSource::kNone;
    HandleAxes axes{};
    HandleButtons buttons{};
    HandleTouchpad touchpad{};
    HandleMotion motion{};
    int64_t ts_us = 0;
};
