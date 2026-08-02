#include "handle/apps/handle_app_keymap.h"

#include "gimbal/Gimbal.h"

#include <esp_log.h>

#if DEEP_DOG_LED_ENABLE
#include "led/led_init.h"
#include "led/led_strip_control.h"
#endif

#define TAG "handle_app_keymap"

HandleAppKeyMap::HandleAppKeyMap(HandleEventHub* hub) : hub_(hub) {
    HandleKeymapInit();
}

void HandleAppKeyMap::Fire(const HandleActionBinding_t& act, bool hold) {
    if (act.id == HK_ACT_NONE) {
        return;
    }

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
    ESP_LOGD(TAG, "led action id=%d ignored (led off)", static_cast<int>(act.id));
#endif
}

void HandleAppKeyMap::OnKeyEdge(HandleKeyIndex_t key, bool now, bool prev) {
    const HandleKeymapState_t* st = HandleKeymapGet();
    const HandleKeyBinding_t& bind = st->keys[key];

    if (now && !prev) {
        /* rising: press once; start hold if configured */
        Fire(bind.press, false);
        if (bind.hold.id != HK_ACT_NONE) {
            Fire(bind.hold, true);
            hold_active_[key] = true;
        }
    } else if (!now && prev) {
        /* falling: stop jog if hold was active */
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

void HandleAppKeyMap::OnSnapshot(const HandleSnapshot& snap) {
    if (!hub_ || !hub_->AppsEnabled()) {
        return;
    }
    const HandleProfile_t profile = HandleKeymapProfile();
    if (profile != HANDLE_PROFILE_LED_DEMO && profile != HANDLE_PROFILE_GIMBAL) {
        /* stop any lingering hold jogs when leaving keymap profiles */
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
}
