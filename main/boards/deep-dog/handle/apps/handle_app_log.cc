#include "handle/apps/handle_app_log.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <cmath>
#include <cstring>

#define TAG "handle_app_log"

namespace {
HandleSnapshot s_last{};
int64_t s_last_log_us = 0;

bool MeaningfulChange(const HandleSnapshot& a, const HandleSnapshot& b) {
    if (a.connected != b.connected || a.source != b.source) {
        return true;
    }
    auto btn = [](const HandleButtons& x, const HandleButtons& y) {
        return x.a != y.a || x.b != y.b || x.x != y.x || x.y != y.y || x.l1 != y.l1 || x.r1 != y.r1 ||
               x.start != y.start || x.select != y.select || x.touch != y.touch ||
               std::fabs(x.l2 - y.l2) > 0.05f || std::fabs(x.r2 - y.r2) > 0.05f;
    };
    if (btn(a.buttons, b.buttons)) {
        return true;
    }
    if (a.touchpad.present != b.touchpad.present || a.touchpad.active != b.touchpad.active ||
        a.touchpad.fingers != b.touchpad.fingers ||
        a.touchpad.contact_count != b.touchpad.contact_count ||
        std::fabs(a.touchpad.x - b.touchpad.x) > 0.02f ||
        std::fabs(a.touchpad.y - b.touchpad.y) > 0.02f) {
        return true;
    }
    for (int i = 0; i < a.touchpad.contact_count && i < 2; ++i) {
        if (a.touchpad.contacts[i].active != b.touchpad.contacts[i].active ||
            std::fabs(a.touchpad.contacts[i].x - b.touchpad.contacts[i].x) > 0.02f ||
            std::fabs(a.touchpad.contacts[i].y - b.touchpad.contacts[i].y) > 0.02f) {
            return true;
        }
    }
    if (a.motion.present != b.motion.present) {
        return true;
    }
    if (a.motion.present) {
        if (std::fabs(a.motion.gyro_x - b.motion.gyro_x) > 2.f ||
            std::fabs(a.motion.gyro_y - b.motion.gyro_y) > 2.f ||
            std::fabs(a.motion.gyro_z - b.motion.gyro_z) > 2.f ||
            std::fabs(a.motion.accel_x - b.motion.accel_x) > 0.05f ||
            std::fabs(a.motion.accel_y - b.motion.accel_y) > 0.05f ||
            std::fabs(a.motion.accel_z - b.motion.accel_z) > 0.05f) {
            return true;
        }
    }
    return std::fabs(a.axes.lx - b.axes.lx) > 0.08f || std::fabs(a.axes.ly - b.axes.ly) > 0.08f ||
           std::fabs(a.axes.rx - b.axes.rx) > 0.08f || std::fabs(a.axes.ry - b.axes.ry) > 0.08f;
}
}  // namespace

void HandleAppLog::OnSnapshot(const HandleSnapshot& snap) {
    const int64_t now = esp_timer_get_time();
    const bool force = (now - s_last_log_us) > 1000000;
    if (!force && !MeaningfulChange(snap, s_last)) {
        return;
    }
    s_last = snap;
    s_last_log_us = now;
    const char* src = HandleSourceName(snap.source);
    if (snap.touchpad.present) {
        ESP_LOGI(TAG,
                 "conn=%d src=%s lx=%.2f ly=%.2f rx=%.2f ry=%.2f "
                 "a=%d b=%d x=%d y=%d l1=%d r1=%d start=%d select=%d touch=%d "
                 "tp={active=%d x=%.2f y=%.2f fingers=%d contacts=%d} "
                 "motion=%d gx=%.1f gy=%.1f gz=%.1f",
                 snap.connected ? 1 : 0,
                 src ? src : "-",
                 snap.axes.lx,
                 snap.axes.ly,
                 snap.axes.rx,
                 snap.axes.ry,
                 snap.buttons.a ? 1 : 0,
                 snap.buttons.b ? 1 : 0,
                 snap.buttons.x ? 1 : 0,
                 snap.buttons.y ? 1 : 0,
                 snap.buttons.l1 ? 1 : 0,
                 snap.buttons.r1 ? 1 : 0,
                 snap.buttons.start ? 1 : 0,
                 snap.buttons.select ? 1 : 0,
                 snap.buttons.touch ? 1 : 0,
                 snap.touchpad.active ? 1 : 0,
                 snap.touchpad.x,
                 snap.touchpad.y,
                 snap.touchpad.fingers,
                 snap.touchpad.contact_count,
                 snap.motion.present ? 1 : 0,
                 snap.motion.gyro_x,
                 snap.motion.gyro_y,
                 snap.motion.gyro_z);
        return;
    }
    ESP_LOGI(TAG,
             "conn=%d src=%s lx=%.2f ly=%.2f rx=%.2f ry=%.2f "
             "a=%d b=%d x=%d y=%d l1=%d r1=%d start=%d select=%d",
             snap.connected ? 1 : 0,
             src ? src : "-",
             snap.axes.lx,
             snap.axes.ly,
             snap.axes.rx,
             snap.axes.ry,
             snap.buttons.a ? 1 : 0,
             snap.buttons.b ? 1 : 0,
             snap.buttons.x ? 1 : 0,
             snap.buttons.y ? 1 : 0,
             snap.buttons.l1 ? 1 : 0,
             snap.buttons.r1 ? 1 : 0,
             snap.buttons.start ? 1 : 0,
             snap.buttons.select ? 1 : 0);
}
