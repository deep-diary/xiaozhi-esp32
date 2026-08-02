#include "gimbal/Gimbal.h"

#include <esp_log.h>
#include <stdlib.h>
#include <string.h>

#define TAG "dog_gimbal"

#if DEEP_DOG_GIMBAL_ENABLE

static Gimbal_t s_bank;
static bool s_bank_ready = false;

static int ClampInt(int v, int lo, int hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static int ClampSpeed(int speed) {
    return ClampInt(speed, DEEP_DOG_GIMBAL_SPEED_MIN, DEEP_DOG_GIMBAL_SPEED_MAX);
}

static uint32_t DurationForDelta(int delta_deg, int speed_deg_s) {
    if (speed_deg_s <= 0) {
        return 0;
    }
    const int ad = abs(delta_deg);
    if (ad == 0) {
        return 0;
    }
    uint32_t ms = (uint32_t)((ad * 1000) / speed_deg_s);
    if (ms < 1) {
        ms = 1;
    }
    return ms;
}

static void Notify(Gimbal_t* g) {
    if (g && g->on_notify) {
        g->on_notify(g->on_notify_ctx);
    }
}

static void ServoUpdateBridge(Servo_t* servo, void* ctx) {
    (void)servo;
    Notify((Gimbal_t*)ctx);
}

static void ApplyPan(Gimbal_t* g, int angle, int speed) {
    const int cur = Servo_read(&g->pan_servo);
    const int tgt = ClampInt(angle, g->pan_servo.min_angle, g->pan_servo.max_angle);
    Servo_writeTimed(&g->pan_servo, tgt, DurationForDelta(tgt - cur, speed));
}

static void ApplyTilt(Gimbal_t* g, int angle, int speed) {
    const int cur = Servo_read(&g->tilt_servo);
    const int tgt = ClampInt(angle, g->tilt_servo.min_angle, g->tilt_servo.max_angle);
    Servo_writeTimed(&g->tilt_servo, tgt, DurationForDelta(tgt - cur, speed));
}

static void JogTick(void* arg) {
    Gimbal_t* g = (Gimbal_t*)arg;
    if (!g || !g->initialized) {
        return;
    }
    bool moved = false;
    const float period_s = (float)DEEP_DOG_GIMBAL_JOG_PERIOD_MS / 1000.0f;
    if (g->jog_pan_dir != 0) {
        const int step = (int)(g->pan_speed * period_s + 0.5f);
        const int d = g->jog_pan_dir * (step > 0 ? step : 1);
        ApplyPan(g, Servo_read(&g->pan_servo) + d, g->pan_speed);
        moved = true;
    }
    if (g->jog_tilt_dir != 0) {
        const int step = (int)(g->tilt_speed * period_s + 0.5f);
        const int d = g->jog_tilt_dir * (step > 0 ? step : 1);
        ApplyTilt(g, Servo_read(&g->tilt_servo) + d, g->tilt_speed);
        moved = true;
    }
    if (moved) {
        Notify(g);
    }
}

static void EnsureJogTimer(Gimbal_t* g) {
    if (g->jog_timer) {
        return;
    }
    esp_timer_create_args_t args = {
        .callback = &JogTick,
        .arg = g,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "gimbal_jog",
        .skip_unhandled_events = true,
    };
    if (esp_timer_create(&args, &g->jog_timer) != ESP_OK) {
        g->jog_timer = NULL;
        ESP_LOGW(TAG, "jog timer create failed");
    }
}

static void RefreshJogTimer(Gimbal_t* g) {
    EnsureJogTimer(g);
    if (!g->jog_timer) {
        return;
    }
    esp_timer_stop(g->jog_timer);
    if (g->jog_pan_dir != 0 || g->jog_tilt_dir != 0) {
        esp_timer_start_periodic(g->jog_timer, (uint64_t)DEEP_DOG_GIMBAL_JOG_PERIOD_MS * 1000ULL);
    }
}

esp_err_t Gimbal_init(Gimbal_t* gimbal, int pan_gpio, int tilt_gpio) {
    if (!gimbal) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(gimbal, 0, sizeof(*gimbal));
    esp_err_t err = Servo_attach(&gimbal->pan_servo, pan_gpio, SERVO_TYPE_270);
    if (err != ESP_OK) {
        return err;
    }
    err = Servo_attach(&gimbal->tilt_servo, tilt_gpio, SERVO_TYPE_180);
    if (err != ESP_OK) {
        Servo_detach(&gimbal->pan_servo);
        return err;
    }
    gimbal->pan_speed = DEEP_DOG_GIMBAL_DEFAULT_PAN_SPEED;
    gimbal->tilt_speed = DEEP_DOG_GIMBAL_DEFAULT_TILT_SPEED;
    gimbal->step_deg = DEEP_DOG_GIMBAL_STEP_DEG;
    gimbal->initialized = true;
    Servo_setUpdateCallback(&gimbal->pan_servo, ServoUpdateBridge, gimbal);
    Servo_setUpdateCallback(&gimbal->tilt_servo, ServoUpdateBridge, gimbal);
    /* center */
    Servo_write(&gimbal->pan_servo, (gimbal->pan_servo.min_angle + gimbal->pan_servo.max_angle) / 2);
    Servo_write(&gimbal->tilt_servo, (gimbal->tilt_servo.min_angle + gimbal->tilt_servo.max_angle) / 2);
    ESP_LOGI(TAG, "Gimbal ready pan=%d tilt=%d", pan_gpio, tilt_gpio);
    Notify(gimbal);
    return ESP_OK;
}

void Gimbal_deinit(Gimbal_t* gimbal) {
    if (!gimbal) {
        return;
    }
    Gimbal_stop(gimbal);
    if (gimbal->jog_timer) {
        esp_timer_stop(gimbal->jog_timer);
        esp_timer_delete(gimbal->jog_timer);
        gimbal->jog_timer = NULL;
    }
    Servo_setUpdateCallback(&gimbal->pan_servo, NULL, NULL);
    Servo_setUpdateCallback(&gimbal->tilt_servo, NULL, NULL);
    Servo_detach(&gimbal->pan_servo);
    Servo_detach(&gimbal->tilt_servo);
    gimbal->initialized = false;
}

bool Gimbal_isInitialized(Gimbal_t* gimbal) {
    return gimbal && gimbal->initialized;
}

void Gimbal_setAngles(Gimbal_t* gimbal, int pan_angle, int tilt_angle) {
    Gimbal_setAnglesTimed(gimbal, pan_angle, tilt_angle, 0);
}

void Gimbal_setAnglesTimed(Gimbal_t* gimbal, int pan_angle, int tilt_angle, int speed_deg_s) {
    if (!Gimbal_isInitialized(gimbal)) {
        return;
    }
    Gimbal_stopJog(gimbal);
    const int spd_pan = speed_deg_s > 0 ? speed_deg_s : gimbal->pan_speed;
    const int spd_tilt = speed_deg_s > 0 ? speed_deg_s : gimbal->tilt_speed;
    if (speed_deg_s == 0) {
        Servo_write(&gimbal->pan_servo,
                    ClampInt(pan_angle, gimbal->pan_servo.min_angle, gimbal->pan_servo.max_angle));
        Servo_write(&gimbal->tilt_servo,
                    ClampInt(tilt_angle, gimbal->tilt_servo.min_angle, gimbal->tilt_servo.max_angle));
    } else {
        ApplyPan(gimbal, pan_angle, spd_pan);
        ApplyTilt(gimbal, tilt_angle, spd_tilt);
    }
    Notify(gimbal);
}

void Gimbal_moveRelative(Gimbal_t* gimbal, int d_pan, int d_tilt, int speed_deg_s) {
    if (!Gimbal_isInitialized(gimbal)) {
        return;
    }
    const int pan = Servo_read(&gimbal->pan_servo) + d_pan;
    const int tilt = Servo_read(&gimbal->tilt_servo) + d_tilt;
    Gimbal_setAnglesTimed(gimbal, pan, tilt, speed_deg_s);
}

void Gimbal_nudgeLeft(Gimbal_t* gimbal) {
    if (!Gimbal_isInitialized(gimbal)) {
        return;
    }
    Gimbal_moveRelative(gimbal, -gimbal->step_deg, 0, gimbal->pan_speed);
}

void Gimbal_nudgeRight(Gimbal_t* gimbal) {
    if (!Gimbal_isInitialized(gimbal)) {
        return;
    }
    Gimbal_moveRelative(gimbal, gimbal->step_deg, 0, gimbal->pan_speed);
}

void Gimbal_nudgeUp(Gimbal_t* gimbal) {
    if (!Gimbal_isInitialized(gimbal)) {
        return;
    }
    Gimbal_moveRelative(gimbal, 0, gimbal->step_deg, gimbal->tilt_speed);
}

void Gimbal_nudgeDown(Gimbal_t* gimbal) {
    if (!Gimbal_isInitialized(gimbal)) {
        return;
    }
    Gimbal_moveRelative(gimbal, 0, -gimbal->step_deg, gimbal->tilt_speed);
}

void Gimbal_startJog(Gimbal_t* gimbal, gimbal_dir_t dir) {
    if (!Gimbal_isInitialized(gimbal)) {
        return;
    }
    switch (dir) {
        case GIMBAL_DIR_LEFT:
            gimbal->jog_pan_dir = -1;
            break;
        case GIMBAL_DIR_RIGHT:
            gimbal->jog_pan_dir = 1;
            break;
        case GIMBAL_DIR_UP:
            gimbal->jog_tilt_dir = 1;
            break;
        case GIMBAL_DIR_DOWN:
            gimbal->jog_tilt_dir = -1;
            break;
        default:
            return;
    }
    RefreshJogTimer(gimbal);
    Notify(gimbal);
}

void Gimbal_stopJog(Gimbal_t* gimbal) {
    if (!gimbal) {
        return;
    }
    gimbal->jog_pan_dir = 0;
    gimbal->jog_tilt_dir = 0;
    RefreshJogTimer(gimbal);
    Notify(gimbal);
}

void Gimbal_stop(Gimbal_t* gimbal) {
    if (!Gimbal_isInitialized(gimbal)) {
        return;
    }
    Gimbal_stopJog(gimbal);
    Servo_write(&gimbal->pan_servo, Servo_read(&gimbal->pan_servo));
    Servo_write(&gimbal->tilt_servo, Servo_read(&gimbal->tilt_servo));
    Notify(gimbal);
}

void Gimbal_setPanSpeed(Gimbal_t* gimbal, int speed_deg_s) {
    if (!Gimbal_isInitialized(gimbal)) {
        return;
    }
    gimbal->pan_speed = ClampSpeed(speed_deg_s);
    Notify(gimbal);
}

void Gimbal_setTiltSpeed(Gimbal_t* gimbal, int speed_deg_s) {
    if (!Gimbal_isInitialized(gimbal)) {
        return;
    }
    gimbal->tilt_speed = ClampSpeed(speed_deg_s);
    Notify(gimbal);
}

void Gimbal_panSpeedUp(Gimbal_t* gimbal) {
    if (!Gimbal_isInitialized(gimbal)) {
        return;
    }
    Gimbal_setPanSpeed(gimbal, gimbal->pan_speed + DEEP_DOG_GIMBAL_SPEED_STEP);
}

void Gimbal_panSpeedDown(Gimbal_t* gimbal) {
    if (!Gimbal_isInitialized(gimbal)) {
        return;
    }
    Gimbal_setPanSpeed(gimbal, gimbal->pan_speed - DEEP_DOG_GIMBAL_SPEED_STEP);
}

void Gimbal_tiltSpeedUp(Gimbal_t* gimbal) {
    if (!Gimbal_isInitialized(gimbal)) {
        return;
    }
    Gimbal_setTiltSpeed(gimbal, gimbal->tilt_speed + DEEP_DOG_GIMBAL_SPEED_STEP);
}

void Gimbal_tiltSpeedDown(Gimbal_t* gimbal) {
    if (!Gimbal_isInitialized(gimbal)) {
        return;
    }
    Gimbal_setTiltSpeed(gimbal, gimbal->tilt_speed - DEEP_DOG_GIMBAL_SPEED_STEP);
}

bool Gimbal_getStatus(Gimbal_t* gimbal, GimbalStatus_t* out) {
    if (!out || !Gimbal_isInitialized(gimbal)) {
        return false;
    }
    out->pan = Servo_read(&gimbal->pan_servo);
    out->tilt = Servo_read(&gimbal->tilt_servo);
    out->pan_speed = gimbal->pan_speed;
    out->tilt_speed = gimbal->tilt_speed;
    out->step_deg = gimbal->step_deg;
    out->moving_pan = Servo_isMoving(&gimbal->pan_servo) || gimbal->jog_pan_dir != 0;
    out->moving_tilt = Servo_isMoving(&gimbal->tilt_servo) || gimbal->jog_tilt_dir != 0;
    out->ready = true;
    out->lim_pan_min = gimbal->pan_servo.min_angle;
    out->lim_pan_max = gimbal->pan_servo.max_angle;
    out->lim_tilt_min = gimbal->tilt_servo.min_angle;
    out->lim_tilt_max = gimbal->tilt_servo.max_angle;
    return true;
}

void Gimbal_setNotifyCallback(Gimbal_t* gimbal, GimbalNotifyCb cb, void* ctx) {
    if (!gimbal) {
        return;
    }
    gimbal->on_notify = cb;
    gimbal->on_notify_ctx = ctx;
}

esp_err_t DeepDogGimbalInit(void) {
    if (s_bank_ready) {
        return ESP_OK;
    }
    esp_err_t err = Gimbal_init(&s_bank, DEEP_DOG_SERVO_PAN_GPIO, DEEP_DOG_SERVO_TILT_GPIO);
    if (err == ESP_OK) {
        s_bank_ready = true;
        ESP_LOGI(TAG, "DeepDogGimbalInit ok pan=%d tilt=%d", DEEP_DOG_SERVO_PAN_GPIO,
                 DEEP_DOG_SERVO_TILT_GPIO);
    } else {
        ESP_LOGE(TAG, "DeepDogGimbalInit failed: %s (pan=%d tilt=%d)", esp_err_to_name(err),
                 DEEP_DOG_SERVO_PAN_GPIO, DEEP_DOG_SERVO_TILT_GPIO);
    }
    return err;
}

void DeepDogGimbalDeinit(void) {
    if (!s_bank_ready) {
        return;
    }
    Gimbal_deinit(&s_bank);
    s_bank_ready = false;
}

bool DeepDogGimbalReady(void) {
    return s_bank_ready && Gimbal_isInitialized(&s_bank);
}

Gimbal_t* DeepDogGimbalGet(void) {
    return s_bank_ready ? &s_bank : NULL;
}

#else

esp_err_t Gimbal_init(Gimbal_t* gimbal, int pan_gpio, int tilt_gpio) {
    (void)gimbal;
    (void)pan_gpio;
    (void)tilt_gpio;
    return ESP_ERR_NOT_SUPPORTED;
}
void Gimbal_deinit(Gimbal_t* gimbal) { (void)gimbal; }
bool Gimbal_isInitialized(Gimbal_t* gimbal) {
    (void)gimbal;
    return false;
}
void Gimbal_setAngles(Gimbal_t* gimbal, int pan_angle, int tilt_angle) {
    (void)gimbal;
    (void)pan_angle;
    (void)tilt_angle;
}
void Gimbal_setAnglesTimed(Gimbal_t* gimbal, int pan_angle, int tilt_angle, int speed_deg_s) {
    (void)gimbal;
    (void)pan_angle;
    (void)tilt_angle;
    (void)speed_deg_s;
}
void Gimbal_moveRelative(Gimbal_t* gimbal, int d_pan, int d_tilt, int speed_deg_s) {
    (void)gimbal;
    (void)d_pan;
    (void)d_tilt;
    (void)speed_deg_s;
}
void Gimbal_nudgeLeft(Gimbal_t* gimbal) { (void)gimbal; }
void Gimbal_nudgeRight(Gimbal_t* gimbal) { (void)gimbal; }
void Gimbal_nudgeUp(Gimbal_t* gimbal) { (void)gimbal; }
void Gimbal_nudgeDown(Gimbal_t* gimbal) { (void)gimbal; }
void Gimbal_startJog(Gimbal_t* gimbal, gimbal_dir_t dir) {
    (void)gimbal;
    (void)dir;
}
void Gimbal_stopJog(Gimbal_t* gimbal) { (void)gimbal; }
void Gimbal_stop(Gimbal_t* gimbal) { (void)gimbal; }
void Gimbal_setPanSpeed(Gimbal_t* gimbal, int speed_deg_s) {
    (void)gimbal;
    (void)speed_deg_s;
}
void Gimbal_setTiltSpeed(Gimbal_t* gimbal, int speed_deg_s) {
    (void)gimbal;
    (void)speed_deg_s;
}
void Gimbal_panSpeedUp(Gimbal_t* gimbal) { (void)gimbal; }
void Gimbal_panSpeedDown(Gimbal_t* gimbal) { (void)gimbal; }
void Gimbal_tiltSpeedUp(Gimbal_t* gimbal) { (void)gimbal; }
void Gimbal_tiltSpeedDown(Gimbal_t* gimbal) { (void)gimbal; }
bool Gimbal_getStatus(Gimbal_t* gimbal, GimbalStatus_t* out) {
    (void)gimbal;
    (void)out;
    return false;
}
void Gimbal_setNotifyCallback(Gimbal_t* gimbal, GimbalNotifyCb cb, void* ctx) {
    (void)gimbal;
    (void)cb;
    (void)ctx;
}
esp_err_t DeepDogGimbalInit(void) { return ESP_ERR_NOT_SUPPORTED; }
void DeepDogGimbalDeinit(void) {}
bool DeepDogGimbalReady(void) { return false; }
Gimbal_t* DeepDogGimbalGet(void) { return NULL; }

#endif
