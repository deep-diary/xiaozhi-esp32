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
    HK_ACT_COUNT
} HandleActionId_t;

typedef struct {
    HandleActionId_t id;
    uint8_t r, g, b, brightness;
} HandleActionBinding_t;

typedef struct {
    HandleActionBinding_t press;
    HandleActionBinding_t hold;
} HandleKeyBinding_t;

typedef struct {
    int schema_ver;
    HandleProfile_t profile;
    bool persist;
    const char* source;  // "nvs" | "default" | "mqtt"
    bool ok;
    HandleKeyBinding_t keys[HANDLE_KEY_COUNT];
} HandleKeymapState_t;

void HandleKeymapInit(void);
const HandleKeymapState_t* HandleKeymapGet(void);
HandleProfile_t HandleKeymapProfile(void);

/**
 * 切换 App 剖面：始终加载该应用默认绑定表（覆盖当前表）。
 * persist=true 时写入 NVS。
 */
bool HandleKeymapSetProfile(HandleProfile_t profile, bool persist);
/** 恢复当前 profile 的出厂默认绑定（不改 profile） */
bool HandleKeymapReset(bool persist);
/** merge bindings from JSON object; returns false if parse failed hard */
bool HandleKeymapSetFromJson(const cJSON* bindings, bool merge, bool persist);

/** Build handle/keymap JSON object (caller cJSON_Delete)；catalog 按当前 profile 过滤 */
cJSON* HandleKeymapBuildJson(void);

const char* HandleKeymapProfileName(HandleProfile_t p);
bool HandleKeymapParseProfile(const char* s, HandleProfile_t* out);
const char* HandleKeymapActionName(HandleActionId_t id);
bool HandleKeymapParseAction(const char* s, HandleActionId_t* out);
const char* HandleKeymapKeyName(HandleKeyIndex_t k);
bool HandleKeymapParseKey(const char* s, HandleKeyIndex_t* out);

/** action 是否属于给定 profile 的 catalog（none 始终 true） */
bool HandleKeymapActionInProfile(HandleActionId_t id, HandleProfile_t profile);

#ifdef __cplusplus
}
#endif
