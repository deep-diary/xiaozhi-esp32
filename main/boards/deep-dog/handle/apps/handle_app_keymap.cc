#include "handle/apps/handle_app_keymap.h"

#include "gimbal/Gimbal.h"

#include <cmath>
#include <esp_log.h>

#if DEEP_DOG_LED_ENABLE
#include "led/led_init.h"
#include "led/led_strip_control.h"
#endif

#if DEEP_DOG_MOTOR_ENABLE
#include "motor/deep_motor.h"
#include "motor/motor_access.h"
#include "motor/protocol_motor.h"
#endif

#ifndef DEEP_DOG_HANDLE_AXIS_DEADZONE
#define DEEP_DOG_HANDLE_AXIS_DEADZONE 0.08f
#endif

#ifndef DEEP_DOG_HANDLE_MOTOR_NUDGE_RAD
#define DEEP_DOG_HANDLE_MOTOR_NUDGE_RAD 0.2f
#endif

#ifndef DEEP_DOG_HANDLE_MOTOR_DEFAULT_SPEED
#define DEEP_DOG_HANDLE_MOTOR_DEFAULT_SPEED 5.0f
#endif

#define TAG "handle_app_keymap"

namespace {

uint8_t ResolveMotorId(uint8_t mid) {
    return mid == 0 ? 1 : mid;
}

float ApplyDeadzone(float u, float dz) {
    const float d = dz > 0.f ? dz : DEEP_DOG_HANDLE_AXIS_DEADZONE;
    if (fabsf(u) < d) {
        return 0.f;
    }
    const float sign = u < 0.f ? -1.f : 1.f;
    const float mag = (fabsf(u) - d) / (1.f - d);
    return sign * mag;
}

#if DEEP_DOG_MOTOR_ENABLE
DeepMotor* MotorOrNull() {
    return DeepDogMotorGet();
}
#endif

}  // namespace

HandleAppKeyMap::HandleAppKeyMap(HandleEventHub* hub) : hub_(hub) {
    HandleKeymapInit();
}

void HandleAppKeyMap::Fire(const HandleActionBinding_t& act, bool hold) {
    if (act.id == HK_ACT_NONE) {
        return;
    }

#if DEEP_DOG_MOTOR_ENABLE
    if (act.id >= HK_ACT_MOTOR_ENABLE && act.id <= HK_ACT_MOTOR_NUDGE_NEG) {
        DeepMotor* motor = MotorOrNull();
        if (!motor) {
            ESP_LOGW(TAG, "motor action ignored (no DeepMotor)");
            return;
        }
        const uint8_t mid = ResolveMotorId(act.motor_id);
        motor->registerMotor(mid);
        switch (act.id) {
            case HK_ACT_MOTOR_ENABLE:
                (void)motor->initializeMotor(mid, DEEP_DOG_HANDLE_MOTOR_DEFAULT_SPEED);
                ESP_LOGI(TAG, "motor.enable id=%u", mid);
                break;
            case HK_ACT_MOTOR_DISABLE:
                MotorProtocol::resetMotor(mid);
                motor->invalidateMotorCommandCache(mid);
                ESP_LOGI(TAG, "motor.disable id=%u", mid);
                break;
            case HK_ACT_MOTOR_POS_ZERO:
                (void)motor->setMotorPosition(mid, 0.f, DEEP_DOG_HANDLE_MOTOR_DEFAULT_SPEED);
                break;
            case HK_ACT_MOTOR_NUDGE_POS: {
                float cur = 0.f;
                (void)motor->getMotorTargetAngle(mid, &cur);
                (void)motor->setMotorPosition(mid, cur + DEEP_DOG_HANDLE_MOTOR_NUDGE_RAD,
                                              DEEP_DOG_HANDLE_MOTOR_DEFAULT_SPEED);
                break;
            }
            case HK_ACT_MOTOR_NUDGE_NEG: {
                float cur = 0.f;
                (void)motor->getMotorTargetAngle(mid, &cur);
                (void)motor->setMotorPosition(mid, cur - DEEP_DOG_HANDLE_MOTOR_NUDGE_RAD,
                                              DEEP_DOG_HANDLE_MOTOR_DEFAULT_SPEED);
                break;
            }
            default:
                break;
        }
        (void)hold;
        return;
    }
#endif

    if (act.id >= HK_ACT_GIMBAL_LEFT && act.id <= HK_ACT_GIMBAL_TILT_SPEED_DOWN) {
#if DEEP_DOG_GIMBAL_ENABLE
        Gimbal_t* g = DeepDogGimbalGet();
        if (!g || !Gimbal_isInitialized(g)) {
            ESP_LOGW(TAG, "gimbal action %s ignored (not ready)", HandleKeymapActionName(act.id));
            return;
        }
        ESP_LOGI(TAG, "gimbal %s (%s)", HandleKeymapActionName(act.id), hold ? "hold/jog" : "press");
        switch (act.id) {
            case HK_ACT_GIMBAL_LEFT:
                if (hold) {
                    Gimbal_startJog(g, GIMBAL_DIR_LEFT);
                } else {
                    Gimbal_nudgeLeft(g);
                }
                break;
            case HK_ACT_GIMBAL_RIGHT:
                if (hold) {
                    Gimbal_startJog(g, GIMBAL_DIR_RIGHT);
                } else {
                    Gimbal_nudgeRight(g);
                }
                break;
            case HK_ACT_GIMBAL_UP:
                if (hold) {
                    Gimbal_startJog(g, GIMBAL_DIR_UP);
                } else {
                    Gimbal_nudgeUp(g);
                }
                break;
            case HK_ACT_GIMBAL_DOWN:
                if (hold) {
                    Gimbal_startJog(g, GIMBAL_DIR_DOWN);
                } else {
                    Gimbal_nudgeDown(g);
                }
                break;
            case HK_ACT_GIMBAL_PAN_SPEED_UP:
                Gimbal_panSpeedUp(g);
                break;
            case HK_ACT_GIMBAL_PAN_SPEED_DOWN:
                Gimbal_panSpeedDown(g);
                break;
            case HK_ACT_GIMBAL_TILT_SPEED_UP:
                Gimbal_tiltSpeedUp(g);
                break;
            case HK_ACT_GIMBAL_TILT_SPEED_DOWN:
                Gimbal_tiltSpeedDown(g);
                break;
            default:
                break;
        }
#else
        (void)hold;
        ESP_LOGW(TAG, "gimbal action ignored (DEEP_DOG_GIMBAL_ENABLE=0)");
#endif
        return;
    }

#if DEEP_DOG_LED_ENABLE
    LedStripControl* led = DeepDogLedGetControl();
    if (!led) {
        return;
    }
    StripColor color {act.r, act.g, act.b};
    StripColor low {0, 0, 0};
    led->SetBrightness(act.brightness, 8);
    switch (act.id) {
        case HK_ACT_LED_OFF:
            led->ApplyOff();
            break;
        case HK_ACT_LED_STATIC:
            led->ApplyStatic(color);
            break;
        case HK_ACT_LED_BLINK:
            led->ApplyBlink(color, 300);
            break;
        case HK_ACT_LED_BREATHE:
            led->ApplyBreathe(low, color, 50);
            break;
        case HK_ACT_LED_SCROLL:
            led->ApplyScroll(low, color, 3, 80);
            break;
        case HK_ACT_LED_SYSTEM:
            led->ApplySystem();
            break;
        default:
            break;
    }
#else
    (void)hold;
#endif
}

void HandleAppKeyMap::FireContinuous(const HandleAxisBinding_t& act, float u) {
    if (act.id == HK_ACT_NONE) {
        return;
    }
#if DEEP_DOG_MOTOR_ENABLE
    if (act.id == HK_ACT_MOTOR_POS_NORM || act.id == HK_ACT_MOTOR_VEL_NORM) {
        DeepMotor* motor = MotorOrNull();
        if (!motor) {
            return;
        }
        const uint8_t mid = ResolveMotorId(act.motor_id);
        motor->registerMotor(mid);
        const float scale = act.scale != 0.f ? act.scale : 1.f;
        if (act.id == HK_ACT_MOTOR_POS_NORM) {
            float uu = u;
            if (uu > 1.f) {
                uu = 1.f;
            }
            if (uu < -1.f) {
                uu = -1.f;
            }
            const float pos = uu * P_MAX * scale;
            (void)motor->setMotorPosition(mid, pos, DEEP_DOG_HANDLE_MOTOR_DEFAULT_SPEED);
        } else {
            float uu = fabsf(u);
            if (uu > 1.f) {
                uu = 1.f;
            }
            (void)motor->setMotorSpeedLimit(mid, uu * V_MAX * scale);
        }
        return;
    }
#else
    (void)u;
#endif
}

void HandleAppKeyMap::OnKeyEdge(HandleKeyIndex_t key, bool now, bool prev) {
    const HandleKeymapState_t* st = HandleKeymapGet();
    const HandleKeyBinding_t& bind = st->keys[key];

    if (now && !prev) {
        Fire(bind.press, false);
        if (bind.hold.id != HK_ACT_NONE) {
            Fire(bind.hold, true);
            hold_active_[key] = true;
        }
    } else if (!now && prev) {
        if (hold_active_[key]) {
            hold_active_[key] = false;
#if DEEP_DOG_GIMBAL_ENABLE
            if (bind.hold.id >= HK_ACT_GIMBAL_LEFT && bind.hold.id <= HK_ACT_GIMBAL_DOWN) {
                Gimbal_t* g = DeepDogGimbalGet();
                if (g) {
                    Gimbal_stopJog(g);
                }
            }
#endif
        }
    }
}

void HandleAppKeyMap::ProcessAxes(const HandleSnapshot& snap) {
    const HandleKeymapState_t* st = HandleKeymapGet();
    const float raw[HANDLE_AXIS_COUNT] = {
        snap.axes.lx, snap.axes.ly, snap.axes.rx, snap.axes.ry, snap.buttons.l2, snap.buttons.r2,
    };
    for (int i = 0; i < HANDLE_AXIS_COUNT; ++i) {
        const HandleAxisBinding_t& bind = st->axes[i];
        if (bind.id == HK_ACT_NONE) {
            continue;
        }
        float u = raw[i];
        if (i <= HANDLE_AXIS_RY) {
            u = ApplyDeadzone(u, bind.deadzone);
        } else {
            /* triggers [0,1]: small deadzone near 0 */
            const float d = bind.deadzone > 0.f ? bind.deadzone : DEEP_DOG_HANDLE_AXIS_DEADZONE;
            if (u < d) {
                u = 0.f;
            } else {
                u = (u - d) / (1.f - d);
            }
        }
        FireContinuous(bind, u);
    }
}

void HandleAppKeyMap::OnSnapshot(const HandleSnapshot& snap) {
    if (!hub_ || !hub_->AppsEnabled()) {
        return;
    }
    const HandleProfile_t profile = HandleKeymapProfile();
    const bool keymap_active = profile == HANDLE_PROFILE_LED_DEMO || profile == HANDLE_PROFILE_GIMBAL ||
                               profile == HANDLE_PROFILE_MOTOR;
    if (!keymap_active) {
        for (int i = 0; i < HANDLE_KEY_COUNT; ++i) {
            if (hold_active_[i]) {
                hold_active_[i] = false;
#if DEEP_DOG_GIMBAL_ENABLE
                Gimbal_t* g = DeepDogGimbalGet();
                if (g) {
                    Gimbal_stopJog(g);
                }
#endif
            }
            prev_[i] = false;
        }
        return;
    }

    if (!snap.connected) {
        for (int i = 0; i < HANDLE_KEY_COUNT; ++i) {
            if (hold_active_[i]) {
                hold_active_[i] = false;
#if DEEP_DOG_GIMBAL_ENABLE
                Gimbal_t* g = DeepDogGimbalGet();
                if (g) {
                    Gimbal_stopJog(g);
                }
#endif
            }
            prev_[i] = false;
        }
        return;
    }

    const bool cur[HANDLE_KEY_COUNT] = {
        snap.buttons.a,
        snap.buttons.b,
        snap.buttons.x,
        snap.buttons.y,
        snap.buttons.l1,
        snap.buttons.r1,
        snap.buttons.start,
        snap.buttons.select,
        snap.buttons.dpad_up,
        snap.buttons.dpad_down,
        snap.buttons.dpad_left,
        snap.buttons.dpad_right,
        snap.buttons.l3,
        snap.buttons.r3,
    };
    for (int i = 0; i < HANDLE_KEY_COUNT; ++i) {
        OnKeyEdge(static_cast<HandleKeyIndex_t>(i), cur[i], prev_[i]);
        prev_[i] = cur[i];
    }
    ProcessAxes(snap);
}
