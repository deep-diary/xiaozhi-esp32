#include "handle/keymap_store.h"

#include <nvs.h>
#include <nvs_flash.h>
#include <esp_log.h>

#include <cstring>
#include <ctime>

#define TAG "handle_keymap"
#define NVS_NS "h_keymap"
#define NVS_KEY "blob"
/** v4: gimbal defaults — face keys dir, dpad speed */
#define SCHEMA_VER 4

namespace {

#pragma pack(push, 1)
struct NvsAction {
    uint8_t id;
    uint8_t r, g, b, brightness;
};

struct NvsKey {
    NvsAction press;
    NvsAction hold;
};

struct NvsBlob {
    uint8_t ver;
    uint8_t profile;
    NvsKey keys[HANDLE_KEY_COUNT];
};
#pragma pack(pop)

HandleKeymapState_t s_state;
bool s_inited = false;

HandleActionBinding_t MakeAct(HandleActionId_t id, uint8_t r = 0, uint8_t g = 0, uint8_t b = 0,
                              uint8_t br = 64) {
    HandleActionBinding_t a {};
    a.id = id;
    a.r = r;
    a.g = g;
    a.b = b;
    a.brightness = br;
    return a;
}

void ClearAllKeys(HandleKeymapState_t* st) {
    for (int i = 0; i < HANDLE_KEY_COUNT; ++i) {
        st->keys[i].press = MakeAct(HK_ACT_NONE);
        st->keys[i].hold = MakeAct(HK_ACT_NONE);
    }
}

void ApplyLedDemoDefaults(HandleKeymapState_t* st) {
    ClearAllKeys(st);
    st->keys[HANDLE_KEY_A].press = MakeAct(HK_ACT_LED_STATIC, 255, 0, 0, 64);
    st->keys[HANDLE_KEY_B].press = MakeAct(HK_ACT_LED_STATIC, 0, 0, 255, 64);
    st->keys[HANDLE_KEY_X].press = MakeAct(HK_ACT_LED_BREATHE, 0, 64, 0, 64);
    st->keys[HANDLE_KEY_Y].press = MakeAct(HK_ACT_LED_OFF);
}

void ApplyGimbalDefaults(HandleKeymapState_t* st) {
    ClearAllKeys(st);
    /* 面键：press 点动 / hold 连动；左侧十字键：press 调速 */
    st->keys[HANDLE_KEY_A].press = MakeAct(HK_ACT_GIMBAL_LEFT);
    st->keys[HANDLE_KEY_A].hold = MakeAct(HK_ACT_GIMBAL_LEFT);
    st->keys[HANDLE_KEY_B].press = MakeAct(HK_ACT_GIMBAL_RIGHT);
    st->keys[HANDLE_KEY_B].hold = MakeAct(HK_ACT_GIMBAL_RIGHT);
    st->keys[HANDLE_KEY_X].press = MakeAct(HK_ACT_GIMBAL_UP);
    st->keys[HANDLE_KEY_X].hold = MakeAct(HK_ACT_GIMBAL_UP);
    st->keys[HANDLE_KEY_Y].press = MakeAct(HK_ACT_GIMBAL_DOWN);
    st->keys[HANDLE_KEY_Y].hold = MakeAct(HK_ACT_GIMBAL_DOWN);
    st->keys[HANDLE_KEY_DPAD_LEFT].press = MakeAct(HK_ACT_GIMBAL_PAN_SPEED_DOWN);
    st->keys[HANDLE_KEY_DPAD_RIGHT].press = MakeAct(HK_ACT_GIMBAL_PAN_SPEED_UP);
    st->keys[HANDLE_KEY_DPAD_UP].press = MakeAct(HK_ACT_GIMBAL_TILT_SPEED_UP);
    st->keys[HANDLE_KEY_DPAD_DOWN].press = MakeAct(HK_ACT_GIMBAL_TILT_SPEED_DOWN);
}

void ApplyProfileDefaults(HandleKeymapState_t* st, HandleProfile_t profile) {
    st->schema_ver = SCHEMA_VER;
    st->profile = profile;
    st->ok = true;
    switch (profile) {
        case HANDLE_PROFILE_LED_DEMO:
            ApplyLedDemoDefaults(st);
            break;
        case HANDLE_PROFILE_GIMBAL:
            ApplyGimbalDefaults(st);
            break;
        case HANDLE_PROFILE_DOG:
        case HANDLE_PROFILE_OFF:
        default:
            ClearAllKeys(st);
            break;
    }
}

bool SaveNvs(const HandleKeymapState_t* st) {
    NvsBlob blob {};
    blob.ver = SCHEMA_VER;
    blob.profile = static_cast<uint8_t>(st->profile);
    for (int i = 0; i < HANDLE_KEY_COUNT; ++i) {
        blob.keys[i].press.id = static_cast<uint8_t>(st->keys[i].press.id);
        blob.keys[i].press.r = st->keys[i].press.r;
        blob.keys[i].press.g = st->keys[i].press.g;
        blob.keys[i].press.b = st->keys[i].press.b;
        blob.keys[i].press.brightness = st->keys[i].press.brightness;
        blob.keys[i].hold.id = static_cast<uint8_t>(st->keys[i].hold.id);
        blob.keys[i].hold.r = st->keys[i].hold.r;
        blob.keys[i].hold.g = st->keys[i].hold.g;
        blob.keys[i].hold.b = st->keys[i].hold.b;
        blob.keys[i].hold.brightness = st->keys[i].hold.brightness;
    }
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open failed");
        return false;
    }
    esp_err_t err = nvs_set_blob(h, NVS_KEY, &blob, sizeof(blob));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err == ESP_OK;
}

bool LoadNvs(HandleKeymapState_t* st) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    NvsBlob blob {};
    size_t len = sizeof(blob);
    esp_err_t err = nvs_get_blob(h, NVS_KEY, &blob, &len);
    nvs_close(h);
    if (err != ESP_OK || len != sizeof(blob) || blob.ver != SCHEMA_VER) {
        return false;
    }
    if (blob.profile > HANDLE_PROFILE_OFF) {
        return false;
    }
    ApplyProfileDefaults(st, static_cast<HandleProfile_t>(blob.profile));
    st->source = "nvs";
    for (int i = 0; i < HANDLE_KEY_COUNT; ++i) {
        if (blob.keys[i].press.id >= HK_ACT_COUNT || blob.keys[i].hold.id >= HK_ACT_COUNT) {
            return false;
        }
        st->keys[i].press.id = static_cast<HandleActionId_t>(blob.keys[i].press.id);
        st->keys[i].press.r = blob.keys[i].press.r;
        st->keys[i].press.g = blob.keys[i].press.g;
        st->keys[i].press.b = blob.keys[i].press.b;
        st->keys[i].press.brightness = blob.keys[i].press.brightness;
        st->keys[i].hold.id = static_cast<HandleActionId_t>(blob.keys[i].hold.id);
        st->keys[i].hold.r = blob.keys[i].hold.r;
        st->keys[i].hold.g = blob.keys[i].hold.g;
        st->keys[i].hold.b = blob.keys[i].hold.b;
        st->keys[i].hold.brightness = blob.keys[i].hold.brightness;
    }
    return true;
}

void ParseActionObject(const cJSON* obj, HandleActionBinding_t* out, bool* ok_flag) {
    if (!out) {
        return;
    }
    *out = MakeAct(HK_ACT_NONE);
    if (!cJSON_IsObject(obj)) {
        return;
    }
    const cJSON* id_j = cJSON_GetObjectItem(obj, "id");
    if (!cJSON_IsString(id_j) || !id_j->valuestring) {
        return;
    }
    HandleActionId_t id = HK_ACT_NONE;
    if (!HandleKeymapParseAction(id_j->valuestring, &id)) {
        ESP_LOGW(TAG, "unknown action id=%s", id_j->valuestring);
        if (ok_flag) {
            *ok_flag = false;
        }
        return;
    }
    out->id = id;
    const cJSON* j = cJSON_GetObjectItem(obj, "r");
    if (cJSON_IsNumber(j)) {
        out->r = static_cast<uint8_t>(j->valueint);
    }
    j = cJSON_GetObjectItem(obj, "g");
    if (cJSON_IsNumber(j)) {
        out->g = static_cast<uint8_t>(j->valueint);
    }
    j = cJSON_GetObjectItem(obj, "b");
    if (cJSON_IsNumber(j)) {
        out->b = static_cast<uint8_t>(j->valueint);
    }
    j = cJSON_GetObjectItem(obj, "brightness");
    if (cJSON_IsNumber(j)) {
        out->brightness = static_cast<uint8_t>(j->valueint);
    }
}

void ParseKeyBinding(const cJSON* obj, HandleKeyBinding_t* out, bool* ok_flag) {
    if (!out) {
        return;
    }
    out->press = MakeAct(HK_ACT_NONE);
    out->hold = MakeAct(HK_ACT_NONE);
    if (!cJSON_IsObject(obj)) {
        return;
    }
    const cJSON* press = cJSON_GetObjectItem(obj, "press");
    const cJSON* hold = cJSON_GetObjectItem(obj, "hold");
    if (cJSON_IsObject(press) || cJSON_IsObject(hold)) {
        if (cJSON_IsObject(press)) {
            ParseActionObject(press, &out->press, ok_flag);
        }
        if (cJSON_IsObject(hold)) {
            ParseActionObject(hold, &out->hold, ok_flag);
        }
        return;
    }
    ParseActionObject(obj, &out->press, ok_flag);
}

cJSON* ActionToJson(const HandleActionBinding_t& a) {
    cJSON* o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "id", HandleKeymapActionName(a.id));
    if (a.id == HK_ACT_LED_STATIC || a.id == HK_ACT_LED_BLINK || a.id == HK_ACT_LED_BREATHE ||
        a.id == HK_ACT_LED_SCROLL) {
        cJSON_AddNumberToObject(o, "r", a.r);
        cJSON_AddNumberToObject(o, "g", a.g);
        cJSON_AddNumberToObject(o, "b", a.b);
        cJSON_AddNumberToObject(o, "brightness", a.brightness);
    }
    return o;
}

void AppendCatalogForProfile(cJSON* catalog, HandleProfile_t profile) {
    cJSON_AddItemToArray(catalog, cJSON_CreateString("none"));
    if (profile == HANDLE_PROFILE_LED_DEMO) {
        for (int i = HK_ACT_LED_OFF; i <= HK_ACT_LED_SYSTEM; ++i) {
            cJSON_AddItemToArray(catalog,
                                 cJSON_CreateString(HandleKeymapActionName(static_cast<HandleActionId_t>(i))));
        }
    } else if (profile == HANDLE_PROFILE_GIMBAL) {
        for (int i = HK_ACT_GIMBAL_LEFT; i <= HK_ACT_GIMBAL_TILT_SPEED_DOWN; ++i) {
            cJSON_AddItemToArray(catalog,
                                 cJSON_CreateString(HandleKeymapActionName(static_cast<HandleActionId_t>(i))));
        }
    }
}

}  // namespace

bool HandleKeymapActionInProfile(HandleActionId_t id, HandleProfile_t profile) {
    if (id == HK_ACT_NONE) {
        return true;
    }
    if (profile == HANDLE_PROFILE_LED_DEMO) {
        return id >= HK_ACT_LED_OFF && id <= HK_ACT_LED_SYSTEM;
    }
    if (profile == HANDLE_PROFILE_GIMBAL) {
        return id >= HK_ACT_GIMBAL_LEFT && id <= HK_ACT_GIMBAL_TILT_SPEED_DOWN;
    }
    return false;
}

void HandleKeymapInit(void) {
    if (s_inited) {
        return;
    }
    if (!LoadNvs(&s_state)) {
        ApplyProfileDefaults(&s_state, HANDLE_PROFILE_LED_DEMO);
        s_state.source = "default";
        SaveNvs(&s_state);
        s_state.persist = true;
        s_state.source = "nvs";
        ESP_LOGI(TAG, "keymap factory default saved (schema=%d)", SCHEMA_VER);
    } else {
        ESP_LOGI(TAG, "keymap loaded from NVS profile=%s", HandleKeymapProfileName(s_state.profile));
    }
    s_inited = true;
}

const HandleKeymapState_t* HandleKeymapGet(void) {
    if (!s_inited) {
        HandleKeymapInit();
    }
    return &s_state;
}

HandleProfile_t HandleKeymapProfile(void) {
    return HandleKeymapGet()->profile;
}

bool HandleKeymapSetProfile(HandleProfile_t profile, bool persist) {
    if (!s_inited) {
        HandleKeymapInit();
    }
    /* 切换应用：始终加载该应用默认功能绑定 */
    ApplyProfileDefaults(&s_state, profile);
    s_state.source = "mqtt";
    s_state.persist = persist ? SaveNvs(&s_state) : false;
    if (persist && s_state.persist) {
        s_state.source = "nvs";
    }
    ESP_LOGI(TAG, "set_profile=%s defaults applied persist=%d", HandleKeymapProfileName(profile),
             persist ? 1 : 0);
    return true;
}

bool HandleKeymapReset(bool persist) {
    if (!s_inited) {
        HandleKeymapInit();
    }
    const HandleProfile_t profile = s_state.profile;
    ApplyProfileDefaults(&s_state, profile);
    s_state.source = "default";
    s_state.persist = persist ? SaveNvs(&s_state) : false;
    if (persist && s_state.persist) {
        s_state.source = "nvs";
    }
    return true;
}

bool HandleKeymapSetFromJson(const cJSON* bindings, bool merge, bool persist) {
    if (!s_inited) {
        HandleKeymapInit();
    }
    if (!cJSON_IsObject(bindings)) {
        return false;
    }
    bool ok = true;
    if (!merge) {
        ClearAllKeys(&s_state);
    }
    const cJSON* child = nullptr;
    cJSON_ArrayForEach(child, bindings) {
        if (!child->string) {
            continue;
        }
        HandleKeyIndex_t ki;
        if (!HandleKeymapParseKey(child->string, &ki)) {
            ESP_LOGW(TAG, "ignore unknown key=%s", child->string);
            ok = false;
            continue;
        }
        ParseKeyBinding(child, &s_state.keys[ki], &ok);
    }
    s_state.ok = ok;
    s_state.source = "mqtt";
    s_state.schema_ver = SCHEMA_VER;
    s_state.persist = persist ? SaveNvs(&s_state) : false;
    if (persist && s_state.persist) {
        s_state.source = "nvs";
    }
    return true;
}

cJSON* HandleKeymapBuildJson(void) {
    const HandleKeymapState_t* st = HandleKeymapGet();
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "schema_ver", st->schema_ver);
    cJSON_AddBoolToObject(root, "ok", st->ok);
    cJSON_AddStringToObject(root, "profile", HandleKeymapProfileName(st->profile));
    cJSON_AddBoolToObject(root, "persist", st->persist);
    cJSON_AddStringToObject(root, "source", st->source ? st->source : "default");

    cJSON* bindable = cJSON_AddArrayToObject(root, "bindable_keys");
    for (int i = 0; i < HANDLE_KEY_COUNT; ++i) {
        cJSON_AddItemToArray(bindable, cJSON_CreateString(HandleKeymapKeyName(static_cast<HandleKeyIndex_t>(i))));
    }

    cJSON* bindings = cJSON_CreateObject();
    for (int i = 0; i < HANDLE_KEY_COUNT; ++i) {
        cJSON* key = cJSON_CreateObject();
        cJSON_AddItemToObject(key, "press", ActionToJson(st->keys[i].press));
        cJSON_AddItemToObject(key, "hold", ActionToJson(st->keys[i].hold));
        cJSON_AddItemToObject(bindings, HandleKeymapKeyName(static_cast<HandleKeyIndex_t>(i)), key);
    }
    cJSON_AddItemToObject(root, "bindings", bindings);

    cJSON* catalog = cJSON_AddArrayToObject(root, "catalog");
    AppendCatalogForProfile(catalog, st->profile);

    cJSON_AddArrayToObject(root, "warnings");
    const time_t now = time(nullptr);
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(now > 1000000000 ? now : 0));
    return root;
}

const char* HandleKeymapProfileName(HandleProfile_t p) {
    switch (p) {
        case HANDLE_PROFILE_LED_DEMO:
            return "led_demo";
        case HANDLE_PROFILE_GIMBAL:
            return "gimbal";
        case HANDLE_PROFILE_DOG:
            return "dog";
        case HANDLE_PROFILE_OFF:
            return "off";
        default:
            return "off";
    }
}

bool HandleKeymapParseProfile(const char* s, HandleProfile_t* out) {
    if (!s || !out) {
        return false;
    }
    if (strcmp(s, "led_demo") == 0) {
        *out = HANDLE_PROFILE_LED_DEMO;
        return true;
    }
    if (strcmp(s, "gimbal") == 0) {
        *out = HANDLE_PROFILE_GIMBAL;
        return true;
    }
    if (strcmp(s, "dog") == 0) {
        *out = HANDLE_PROFILE_DOG;
        return true;
    }
    if (strcmp(s, "off") == 0) {
        *out = HANDLE_PROFILE_OFF;
        return true;
    }
    return false;
}

const char* HandleKeymapActionName(HandleActionId_t id) {
    switch (id) {
        case HK_ACT_NONE:
            return "none";
        case HK_ACT_LED_OFF:
            return "led.off";
        case HK_ACT_LED_STATIC:
            return "led.static";
        case HK_ACT_LED_BLINK:
            return "led.blink";
        case HK_ACT_LED_BREATHE:
            return "led.breathe";
        case HK_ACT_LED_SCROLL:
            return "led.scroll";
        case HK_ACT_LED_SYSTEM:
            return "led.system";
        case HK_ACT_GIMBAL_LEFT:
            return "gimbal.left";
        case HK_ACT_GIMBAL_RIGHT:
            return "gimbal.right";
        case HK_ACT_GIMBAL_UP:
            return "gimbal.up";
        case HK_ACT_GIMBAL_DOWN:
            return "gimbal.down";
        case HK_ACT_GIMBAL_PAN_SPEED_UP:
            return "gimbal.pan_speed_up";
        case HK_ACT_GIMBAL_PAN_SPEED_DOWN:
            return "gimbal.pan_speed_down";
        case HK_ACT_GIMBAL_TILT_SPEED_UP:
            return "gimbal.tilt_speed_up";
        case HK_ACT_GIMBAL_TILT_SPEED_DOWN:
            return "gimbal.tilt_speed_down";
        default:
            return "none";
    }
}

bool HandleKeymapParseAction(const char* s, HandleActionId_t* out) {
    if (!s || !out) {
        return false;
    }
    for (int i = 0; i < HK_ACT_COUNT; ++i) {
        if (strcmp(s, HandleKeymapActionName(static_cast<HandleActionId_t>(i))) == 0) {
            *out = static_cast<HandleActionId_t>(i);
            return true;
        }
    }
    return false;
}

const char* HandleKeymapKeyName(HandleKeyIndex_t k) {
    switch (k) {
        case HANDLE_KEY_A:
            return "a";
        case HANDLE_KEY_B:
            return "b";
        case HANDLE_KEY_X:
            return "x";
        case HANDLE_KEY_Y:
            return "y";
        case HANDLE_KEY_L1:
            return "l1";
        case HANDLE_KEY_R1:
            return "r1";
        case HANDLE_KEY_START:
            return "start";
        case HANDLE_KEY_SELECT:
            return "select";
        case HANDLE_KEY_DPAD_UP:
            return "dpad_up";
        case HANDLE_KEY_DPAD_DOWN:
            return "dpad_down";
        case HANDLE_KEY_DPAD_LEFT:
            return "dpad_left";
        case HANDLE_KEY_DPAD_RIGHT:
            return "dpad_right";
        case HANDLE_KEY_L3:
            return "l3";
        case HANDLE_KEY_R3:
            return "r3";
        default:
            return "";
    }
}

bool HandleKeymapParseKey(const char* s, HandleKeyIndex_t* out) {
    if (!s || !out) {
        return false;
    }
    for (int i = 0; i < HANDLE_KEY_COUNT; ++i) {
        if (strcmp(s, HandleKeymapKeyName(static_cast<HandleKeyIndex_t>(i))) == 0) {
            *out = static_cast<HandleKeyIndex_t>(i);
            return true;
        }
    }
    return false;
}
