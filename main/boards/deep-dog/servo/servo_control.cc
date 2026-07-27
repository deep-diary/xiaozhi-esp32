#include "servo/servo_control.h"

#include "servo/servo_config.h"

#include <esp_log.h>
#include <string.h>

#define TAG "dog_servo_bank"

#if DEEP_DOG_SERVO_ENABLE

namespace {

Servo_t g_servos[DEEP_DOG_SERVO_COUNT];
int g_gpio[DEEP_DOG_SERVO_COUNT] = {
    DEEP_DOG_SERVO_PAN_GPIO,
    DEEP_DOG_SERVO_TILT_GPIO,
};
bool g_ready = false;
DeepDogServoBankNotifyCb g_notify_cb = nullptr;
void* g_notify_ctx = nullptr;

void OnServoUpdate(Servo_t* /*servo*/, void* /*ctx*/) {
    if (g_notify_cb) {
        g_notify_cb(g_notify_ctx);
    }
}

int TypeToInt(servo_type_t t) {
    switch (t) {
        case SERVO_TYPE_90:
            return 90;
        case SERVO_TYPE_270:
            return 270;
        case SERVO_TYPE_360:
            return 360;
        case SERVO_TYPE_180:
        default:
            return 180;
    }
}

bool ValidIndex(int index) {
    return index >= 0 && index < DEEP_DOG_SERVO_COUNT;
}

void RefreshReady() {
    g_ready = false;
    for (int i = 0; i < DEEP_DOG_SERVO_COUNT; ++i) {
        if (Servo_attached(&g_servos[i])) {
            g_ready = true;
            return;
        }
    }
}

}  // namespace

esp_err_t DeepDogServoInit(void) {
    if (g_ready) {
        return ESP_OK;
    }

#if !DEEP_DOG_PWM_AVAILABLE
    ESP_LOGW(TAG, "PWM not available (EXT_PIN != PWM)");
    return ESP_ERR_NOT_SUPPORTED;
#else
    memset(g_servos, 0, sizeof(g_servos));
    for (int i = 0; i < DEEP_DOG_SERVO_COUNT; ++i) {
        esp_err_t err = Servo_attach(&g_servos[i], g_gpio[i], SERVO_TYPE_180);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "attach index=%d gpio=%d failed: %s", i, g_gpio[i], esp_err_to_name(err));
            for (int j = 0; j < i; ++j) {
                Servo_detach(&g_servos[j]);
            }
            return err;
        }
        Servo_setUpdateCallback(&g_servos[i], &OnServoUpdate, nullptr);
    }
    g_ready = true;
    ESP_LOGI(TAG, "Servo bank ready count=%d gpio=[%d,%d]", DEEP_DOG_SERVO_COUNT, g_gpio[0], g_gpio[1]);
    return ESP_OK;
#endif
}

void DeepDogServoDeinit(void) {
    if (!g_ready) {
        return;
    }
    for (int i = 0; i < DEEP_DOG_SERVO_COUNT; ++i) {
        Servo_detach(&g_servos[i]);
    }
    g_ready = false;
}

bool DeepDogServoReady(void) {
    return g_ready;
}

esp_err_t DeepDogServoSetAngle(int index, int angle, uint32_t duration_ms) {
    if (!g_ready || !ValidIndex(index)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!Servo_attached(&g_servos[index])) {
        return ESP_ERR_INVALID_STATE;
    }
    Servo_writeTimed(&g_servos[index], angle, duration_ms);
    return ESP_OK;
}

esp_err_t DeepDogServoSetType(int index, servo_type_t type) {
    if (!g_ready || !ValidIndex(index)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!Servo_attached(&g_servos[index])) {
        return ESP_ERR_INVALID_STATE;
    }
    Servo_setType(&g_servos[index], type);
    return ESP_OK;
}

esp_err_t DeepDogServoAttach(int index, servo_type_t type) {
    if (!ValidIndex(index)) {
        return ESP_ERR_INVALID_ARG;
    }
#if !DEEP_DOG_PWM_AVAILABLE
    return ESP_ERR_NOT_SUPPORTED;
#else
    esp_err_t err = Servo_attach(&g_servos[index], g_gpio[index], type);
    if (err == ESP_OK) {
        Servo_setUpdateCallback(&g_servos[index], &OnServoUpdate, nullptr);
        RefreshReady();
    }
    return err;
#endif
}

esp_err_t DeepDogServoDetach(int index) {
    if (!ValidIndex(index)) {
        return ESP_ERR_INVALID_ARG;
    }
    Servo_detach(&g_servos[index]);
    RefreshReady();
    return ESP_OK;
}

bool DeepDogServoGetSnapshot(int index, DeepDogServoSnapshot* out) {
    if (!out || !ValidIndex(index)) {
        return false;
    }
    Servo_t* s = &g_servos[index];
    out->index = index;
    out->attached = Servo_attached(s);
    out->angle = Servo_read(s);
    out->target = Servo_target(s);
    out->min_angle = s->min_angle;
    out->max_angle = s->max_angle;
    out->type = TypeToInt(s->type);
    out->moving = Servo_isMoving(s);
    return true;
}

int DeepDogServoCount(void) {
    return DEEP_DOG_SERVO_COUNT;
}

void DeepDogServoSetNotifyCallback(DeepDogServoBankNotifyCb cb, void* ctx) {
    g_notify_cb = cb;
    g_notify_ctx = ctx;
}

#else

esp_err_t DeepDogServoInit(void) {
    return ESP_ERR_NOT_SUPPORTED;
}
void DeepDogServoDeinit(void) {}
bool DeepDogServoReady(void) {
    return false;
}
esp_err_t DeepDogServoSetAngle(int index, int angle, uint32_t duration_ms) {
    (void)index;
    (void)angle;
    (void)duration_ms;
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t DeepDogServoSetType(int index, servo_type_t type) {
    (void)index;
    (void)type;
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t DeepDogServoAttach(int index, servo_type_t type) {
    (void)index;
    (void)type;
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t DeepDogServoDetach(int index) {
    (void)index;
    return ESP_ERR_NOT_SUPPORTED;
}
bool DeepDogServoGetSnapshot(int index, DeepDogServoSnapshot* out) {
    (void)index;
    (void)out;
    return false;
}
int DeepDogServoCount(void) {
    return DEEP_DOG_SERVO_COUNT;
}
void DeepDogServoSetNotifyCallback(DeepDogServoBankNotifyCb cb, void* ctx) {
    (void)cb;
    (void)ctx;
}

#endif
