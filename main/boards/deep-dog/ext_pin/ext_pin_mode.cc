#include "ext_pin/ext_pin_mode.h"

#include <cstring>

#include <esp_log.h>
#include <nvs.h>

namespace deep_dog_ext_pin {
namespace {

constexpr const char* TAG = "ExtPin";
constexpr const char* kNvsNamespace = "deep_dog_iomode";
constexpr const char* kKeyActive = "active";
constexpr const char* kKeyPending = "pending";

/** 顺序即 supported_modes 上报顺序 */
const deep_dog_ext_pin_mode_t kSupportedModes[] = {
    DEEP_DOG_EXT_PIN_CAN,  DEEP_DOG_EXT_PIN_PWM,  DEEP_DOG_EXT_PIN_LED, DEEP_DOG_EXT_PIN_UART,
    DEEP_DOG_EXT_PIN_RS485, DEEP_DOG_EXT_PIN_IO,  DEEP_DOG_EXT_PIN_AD,  DEEP_DOG_EXT_PIN_NONE,
};

struct ModeName {
    deep_dog_ext_pin_mode_t mode;
    const char* name;
};

const ModeName kModeNames[] = {
    {DEEP_DOG_EXT_PIN_NONE, "none"},  {DEEP_DOG_EXT_PIN_CAN, "can"},   {DEEP_DOG_EXT_PIN_UART, "uart"},
    {DEEP_DOG_EXT_PIN_RS485, "rs485"}, {DEEP_DOG_EXT_PIN_PWM, "pwm"},   {DEEP_DOG_EXT_PIN_IO, "io"},
    {DEEP_DOG_EXT_PIN_AD, "ad"},      {DEEP_DOG_EXT_PIN_LED, "led"},
};

/** NVS 读写（直接用 nvs API，避免依赖 Settings 的行为约定） */
bool NvsReadMode(const char* key, deep_dog_ext_pin_mode_t* out) {
    nvs_handle_t h = 0;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    uint8_t raw = 0;
    const esp_err_t err = nvs_get_u8(h, key, &raw);
    nvs_close(h);
    if (err != ESP_OK) {
        return false;
    }
    *out = static_cast<deep_dog_ext_pin_mode_t>(raw);
    return true;
}

esp_err_t NvsWriteMode(const char* key, deep_dog_ext_pin_mode_t mode) {
    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(h, key, static_cast<uint8_t>(mode));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t NvsErase(const char* key) {
    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_key(h, key);
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

deep_dog_ext_pin_mode_t g_active = DEEP_DOG_EXT_PIN_NONE;
bool g_active_set = false;
const char* g_last_apply_error = "";

}  // namespace

deep_dog_ext_pin_mode_t DefaultMode() {
    return DEEP_DOG_EXT_PIN_MODE;
}

const deep_dog_ext_pin_mode_t* SupportedModes(size_t* count) {
    if (count) {
        *count = sizeof(kSupportedModes) / sizeof(kSupportedModes[0]);
    }
    return kSupportedModes;
}

bool IsSupported(deep_dog_ext_pin_mode_t mode) {
    for (const auto m : kSupportedModes) {
        if (m == mode) {
            return true;
        }
    }
    return false;
}

const char* ModeToStr(deep_dog_ext_pin_mode_t mode) {
    for (const auto& item : kModeNames) {
        if (item.mode == mode) {
            return item.name;
        }
    }
    return "unknown";
}

bool StrToMode(const char* str, deep_dog_ext_pin_mode_t* out) {
    if (!str || !out) {
        return false;
    }
    for (const auto& item : kModeNames) {
        if (strcmp(str, item.name) == 0) {
            *out = item.mode;
            return true;
        }
    }
    return false;
}

deep_dog_ext_pin_mode_t ResolveBootTarget() {
    deep_dog_ext_pin_mode_t mode = kNoPending;
    if (NvsReadMode(kKeyPending, &mode) && IsSupported(mode)) {
        ESP_LOGI(TAG, "boot target from pending: %s", ModeToStr(mode));
        return mode;
    }
    if (NvsReadMode(kKeyActive, &mode) && IsSupported(mode)) {
        ESP_LOGI(TAG, "boot target from last active: %s", ModeToStr(mode));
        return mode;
    }
    ESP_LOGI(TAG, "boot target from compile default: %s", ModeToStr(DefaultMode()));
    return DefaultMode();
}

deep_dog_ext_pin_mode_t RollbackMode(deep_dog_ext_pin_mode_t failed) {
    deep_dog_ext_pin_mode_t last = kNoPending;
    if (NvsReadMode(kKeyActive, &last) && IsSupported(last) && last != failed) {
        return last;
    }
    return DEEP_DOG_EXT_PIN_NONE;
}

void CommitApplied(deep_dog_ext_pin_mode_t mode) {
    g_active = mode;
    g_active_set = true;
    const esp_err_t err = NvsWriteMode(kKeyActive, mode);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "persist active mode %s failed: %s", ModeToStr(mode), esp_err_to_name(err));
    }
    ClearPending();
    ESP_LOGI(TAG, "ext_pins active=%s (persisted)", ModeToStr(mode));
}

void MarkApplyFailure(const char* code) {
    g_last_apply_error = (code && code[0]) ? code : "E_APPLY_FAILED";
    ESP_LOGW(TAG, "ext_pins apply failure: %s", g_last_apply_error);
}

const char* LastApplyError() {
    return g_last_apply_error;
}

deep_dog_ext_pin_mode_t ActiveMode() {
    return g_active_set ? g_active : DefaultMode();
}

bool IsActive(deep_dog_ext_pin_mode_t mode) {
    return ActiveMode() == mode;
}

deep_dog_ext_pin_mode_t PendingMode() {
    deep_dog_ext_pin_mode_t mode = kNoPending;
    if (NvsReadMode(kKeyPending, &mode) && IsSupported(mode)) {
        return mode;
    }
    return kNoPending;
}

esp_err_t SetPendingMode(deep_dog_ext_pin_mode_t mode) {
    if (!IsSupported(mode)) {
        ESP_LOGW(TAG, "reject unsupported mode %d", static_cast<int>(mode));
        return ESP_ERR_INVALID_ARG;
    }
    if (PendingMode() == mode) {
        return ESP_OK;  // 幂等
    }
    const esp_err_t err = NvsWriteMode(kKeyPending, mode);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "pending mode set to %s (apply after restart)", ModeToStr(mode));
    }
    return err;
}

void ClearPending() {
    (void)NvsErase(kKeyPending);
}

const char* ApplyPolicy() {
    return "restart";
}

}  // namespace deep_dog_ext_pin
