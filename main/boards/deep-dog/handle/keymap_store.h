#pragma once

#include <cJSON.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HANDLE_PROFILE_LED_DEMO = 0,
    HANDLE_PROFILE_GIMBAL,
    HANDLE_PROFILE_MOTOR,
    HANDLE_PROFILE_DOG,
    HANDLE_PROFILE_OFF,
} HandleProfile_t;

/** 可绑定离散 bool 键（不含 l2/r2 模拟量） */
typedef enum {
    HANDLE_KEY_A = 0,
    HANDLE_KEY_B,
    HANDLE_KEY_X,
    HANDLE_KEY_Y,
    HANDLE_KEY_L1,
    HANDLE_KEY_R1,
    HANDLE_KEY_START,
    HANDLE_KEY_SELECT,
    HANDLE_KEY_DPAD_UP,
    HANDLE_KEY_DPAD_DOWN,
    HANDLE_KEY_DPAD_LEFT,
    HANDLE_KEY_DPAD_RIGHT,
    HANDLE_KEY_L3,
    HANDLE_KEY_R3,
    HANDLE_KEY_COUNT
} HandleKeyIndex_t;

/** 可绑定轴（I08b） */
typedef enum {
    HANDLE_AXIS_LX = 0,
    HANDLE_AXIS_LY,
    HANDLE_AXIS_RX,
    HANDLE_AXIS_RY,
    HANDLE_AXIS_L2,
    HANDLE_AXIS_R2,
    HANDLE_AXIS_COUNT
} HandleAxisIndex_t;

/** Catalog action ids (stored as small ints) */
typedef enum {
    HK_ACT_NONE = 0,
    HK_ACT_LED_OFF,
    HK_ACT_LED_STATIC,
    HK_ACT_LED_BLINK,
    HK_ACT_LED_BREATHE,
    HK_ACT_LED_SCROLL,
    HK_ACT_LED_SYSTEM,
    HK_ACT_GIMBAL_LEFT,
    HK_ACT_GIMBAL_RIGHT,
    HK_ACT_GIMBAL_UP,
    HK_ACT_GIMBAL_DOWN,
    HK_ACT_GIMBAL_PAN_SPEED_UP,
    HK_ACT_GIMBAL_PAN_SPEED_DOWN,
    HK_ACT_GIMBAL_TILT_SPEED_UP,
    HK_ACT_GIMBAL_TILT_SPEED_DOWN,
    HK_ACT_GIMBAL_HOME,
    HK_ACT_MOTOR_ENABLE,
    HK_ACT_MOTOR_DISABLE,
    HK_ACT_MOTOR_POS_ZERO,
    HK_ACT_MOTOR_NUDGE_POS,
    HK_ACT_MOTOR_NUDGE_NEG,
    HK_ACT_MOTOR_POS_NORM,
    HK_ACT_MOTOR_VEL_NORM,
    /** Axis continuous (append at end to keep NVS motor ids stable) */
    HK_ACT_GIMBAL_PAN_RATE,
    HK_ACT_GIMBAL_TILT_RATE,
    HK_ACT_COUNT
} HandleActionId_t;

typedef struct {
    HandleActionId_t id;
    uint8_t r, g, b, brightness;
    uint8_t motor_id; /* 0 → default 1 */
} HandleActionBinding_t;

typedef struct {
    HandleActionBinding_t press;
    HandleActionBinding_t hold;
} HandleKeyBinding_t;

typedef struct {
    HandleActionId_t id;
    uint8_t motor_id; /* 0 → default 1 */
    float scale;      /* default 1 */
    float deadzone;   /* 0 → use firmware default */
} HandleAxisBinding_t;

typedef struct {
    int schema_ver;
    HandleProfile_t profile;
    bool persist;
    const char* source;  // "nvs" | "default" | "mqtt"
    bool ok;
    HandleKeyBinding_t keys[HANDLE_KEY_COUNT];
    HandleAxisBinding_t axes[HANDLE_AXIS_COUNT];
} HandleKeymapState_t;

void HandleKeymapInit(void);
const HandleKeymapState_t* HandleKeymapGet(void);
HandleProfile_t HandleKeymapProfile(void);

bool HandleKeymapSetProfile(HandleProfile_t profile, bool persist);
bool HandleKeymapReset(bool persist);
/** merge discrete + optional axis bindings from JSON objects */
bool HandleKeymapSetFromJson(const cJSON* bindings, const cJSON* axis_bindings, bool merge, bool persist);

cJSON* HandleKeymapBuildJson(void);

const char* HandleKeymapProfileName(HandleProfile_t p);
bool HandleKeymapParseProfile(const char* s, HandleProfile_t* out);
const char* HandleKeymapActionName(HandleActionId_t id);
bool HandleKeymapParseAction(const char* s, HandleActionId_t* out);
const char* HandleKeymapKeyName(HandleKeyIndex_t k);
bool HandleKeymapParseKey(const char* s, HandleKeyIndex_t* out);
const char* HandleKeymapAxisName(HandleAxisIndex_t a);
bool HandleKeymapParseAxis(const char* s, HandleAxisIndex_t* out);

bool HandleKeymapActionInProfile(HandleActionId_t id, HandleProfile_t profile);

#ifdef __cplusplus
}
#endif
