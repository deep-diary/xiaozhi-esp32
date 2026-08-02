#include "servo/Servo.h"

#include <esp_log.h>
#include <math.h>
#include <string.h>

#define TAG "dog_servo"

/* 底层 MCPWM 驱动：裸舵机调试 或 云台产品路径都会用到 */
#if DEEP_DOG_SERVO_ENABLE || DEEP_DOG_GIMBAL_ENABLE

mcpwm_timer_handle_t g_servo_timer = NULL;
bool g_servo_timer_initialized = false;

#define SERVO_MOVE_TICK_US 10000  // 10ms

static inline uint32_t angle_to_compare(int angle, int min_angle, int max_angle) {
    if (angle < min_angle) {
        angle = min_angle;
    }
    if (angle > max_angle) {
        angle = max_angle;
    }
    if (max_angle <= min_angle) {
        return SERVO_MIN_PULSEWIDTH_US;
    }
    return (uint32_t)((angle - min_angle) * (SERVO_MAX_PULSEWIDTH_US - SERVO_MIN_PULSEWIDTH_US) /
                          (max_angle - min_angle) +
                      SERVO_MIN_PULSEWIDTH_US);
}

static inline float ease_out_cubic(float t) {
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

static void notify_update(Servo_t* servo) {
    if (servo && servo->on_update) {
        servo->on_update(servo, servo->on_update_ctx);
    }
}

static void apply_angle_pwm(Servo_t* servo, int angle) {
    if (!servo || !servo->attached || !servo->comparator) {
        return;
    }
    if (angle < servo->min_angle) {
        angle = servo->min_angle;
    }
    if (angle > servo->max_angle) {
        angle = servo->max_angle;
    }
    esp_err_t ret =
        mcpwm_comparator_set_compare_value(servo->comparator, angle_to_compare(angle, servo->min_angle, servo->max_angle));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "set compare failed: %s", esp_err_to_name(ret));
        return;
    }
    servo->current_angle = angle;
}

static void stop_move_timer(Servo_t* servo) {
    if (!servo) {
        return;
    }
    if (servo->move_timer) {
        esp_timer_stop(servo->move_timer);
    }
    servo->moving = false;
}

static void move_timer_cb(void* arg) {
    Servo_t* servo = (Servo_t*)arg;
    if (!servo || !servo->attached || !servo->moving) {
        return;
    }

    const int64_t now = esp_timer_get_time();
    const int64_t elapsed_us = now - servo->move_start_us;
    const int64_t total_us = (int64_t)servo->move_duration_ms * 1000LL;

    if (total_us <= 0 || elapsed_us >= total_us) {
        apply_angle_pwm(servo, servo->target_angle);
        stop_move_timer(servo);
        notify_update(servo);
        return;
    }

    float t = (float)elapsed_us / (float)total_us;
    if (t < 0.f) {
        t = 0.f;
    }
    if (t > 1.f) {
        t = 1.f;
    }
    const float eased = ease_out_cubic(t);
    const int angle =
        (int)lroundf((float)servo->move_start_angle +
                     ((float)servo->target_angle - (float)servo->move_start_angle) * eased);
    apply_angle_pwm(servo, angle);
    notify_update(servo);
}

static esp_err_t ensure_move_timer(Servo_t* servo) {
    if (!servo) {
        return ESP_ERR_INVALID_ARG;
    }
    if (servo->move_timer) {
        return ESP_OK;
    }
    esp_timer_create_args_t args = {
        .callback = &move_timer_cb,
        .arg = servo,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "servo_move",
        .skip_unhandled_events = true,
    };
    return esp_timer_create(&args, &servo->move_timer);
}

static esp_err_t init_servo_timer(void) {
    if (g_servo_timer_initialized) {
        return ESP_OK;
    }

    mcpwm_timer_config_t timer_config = {
        .group_id = 0,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = SERVO_TIMEBASE_RESOLUTION_HZ,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
        .period_ticks = SERVO_TIMEBASE_PERIOD,
        .intr_priority = 0,
        .flags =
            {
                .update_period_on_empty = false,
                .update_period_on_sync = false,
                .allow_pd = false,
            },
    };

    esp_err_t ret = mcpwm_new_timer(&timer_config, &g_servo_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "create timer failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = mcpwm_timer_enable(g_servo_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "enable timer failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = mcpwm_timer_start_stop(g_servo_timer, MCPWM_TIMER_START_NO_STOP);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "start timer failed: %s", esp_err_to_name(ret));
        return ret;
    }

    g_servo_timer_initialized = true;
    ESP_LOGI(TAG, "MCPWM servo timer ready (50Hz)");
    return ESP_OK;
}

static void apply_type_range(Servo_t* servo, servo_type_t type) {
    servo->type = type;
    switch (type) {
        case SERVO_TYPE_90:
            servo->min_angle = 0;
            servo->max_angle = 90;
            break;
        case SERVO_TYPE_270:
            servo->min_angle = 0;
            servo->max_angle = 270;
            break;
        case SERVO_TYPE_360:
            servo->min_angle = 0;
            servo->max_angle = 360;
            break;
        case SERVO_TYPE_180:
        default:
            servo->type = SERVO_TYPE_180;
            servo->min_angle = 0;
            servo->max_angle = 180;
            break;
    }
}

esp_err_t Servo_attach(Servo_t* servo, int gpio_pin, servo_type_t type) {
    if (!servo) {
        return ESP_ERR_INVALID_ARG;
    }

    if (servo->attached) {
        Servo_detach(servo);
    }

    memset(servo, 0, sizeof(Servo_t));
    servo->gpio_pin = gpio_pin;
    apply_type_range(servo, type);
    servo->current_angle = servo->max_angle / 2;
    servo->target_angle = servo->current_angle;

    esp_err_t ret = init_servo_timer();
    if (ret != ESP_OK) {
        return ret;
    }

    mcpwm_operator_config_t operator_config = {
        .group_id = 0,
        .intr_priority = 0,
        .flags = {},
    };
    ret = mcpwm_new_operator(&operator_config, &servo->oper);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "create operator failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = mcpwm_operator_connect_timer(servo->oper, g_servo_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "connect operator failed: %s", esp_err_to_name(ret));
        mcpwm_del_operator(servo->oper);
        servo->oper = NULL;
        return ret;
    }

    mcpwm_comparator_config_t comparator_config = {
        .flags =
            {
                .update_cmp_on_tez = true,
            },
    };
    ret = mcpwm_new_comparator(servo->oper, &comparator_config, &servo->comparator);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "create comparator failed: %s", esp_err_to_name(ret));
        mcpwm_del_operator(servo->oper);
        servo->oper = NULL;
        return ret;
    }

    mcpwm_generator_config_t generator_config = {
        .gen_gpio_num = gpio_pin,
        .flags = {},
    };
    ret = mcpwm_new_generator(servo->oper, &generator_config, &servo->generator);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "create generator failed: %s", esp_err_to_name(ret));
        mcpwm_del_comparator(servo->comparator);
        mcpwm_del_operator(servo->oper);
        servo->comparator = NULL;
        servo->oper = NULL;
        return ret;
    }

    ret = mcpwm_generator_set_action_on_timer_event(
        servo->generator,
        MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "timer action failed: %s", esp_err_to_name(ret));
        Servo_detach(servo);
        return ret;
    }

    ret = mcpwm_generator_set_action_on_compare_event(
        servo->generator,
        MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, servo->comparator, MCPWM_GEN_ACTION_LOW));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "compare action failed: %s", esp_err_to_name(ret));
        Servo_detach(servo);
        return ret;
    }

    ret = mcpwm_comparator_set_compare_value(
        servo->comparator, angle_to_compare(servo->current_angle, servo->min_angle, servo->max_angle));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "initial compare failed: %s", esp_err_to_name(ret));
        Servo_detach(servo);
        return ret;
    }

    ret = ensure_move_timer(servo);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "move timer create failed: %s", esp_err_to_name(ret));
        Servo_detach(servo);
        return ret;
    }

    servo->attached = true;
    ESP_LOGI(TAG, "Servo attached gpio=%d type=%d range=%d-%d angle=%d", gpio_pin, (int)servo->type,
             servo->min_angle, servo->max_angle, servo->current_angle);
    return ESP_OK;
}

void Servo_detach(Servo_t* servo) {
    if (!servo) {
        return;
    }

    stop_move_timer(servo);
    if (servo->move_timer) {
        esp_timer_delete(servo->move_timer);
        servo->move_timer = NULL;
    }

    if (servo->generator) {
        mcpwm_del_generator(servo->generator);
        servo->generator = NULL;
    }
    if (servo->comparator) {
        mcpwm_del_comparator(servo->comparator);
        servo->comparator = NULL;
    }
    if (servo->oper) {
        mcpwm_del_operator(servo->oper);
        servo->oper = NULL;
    }

    servo->attached = false;
    ESP_LOGI(TAG, "Servo detached gpio=%d", servo->gpio_pin);
}

void Servo_write(Servo_t* servo, int angle) {
    Servo_writeTimed(servo, angle, 0);
}

void Servo_writeTimed(Servo_t* servo, int angle, uint32_t duration_ms) {
    if (!servo || !servo->attached) {
        return;
    }

    if (angle < servo->min_angle) {
        angle = servo->min_angle;
    }
    if (angle > servo->max_angle) {
        angle = servo->max_angle;
    }

    stop_move_timer(servo);
    servo->target_angle = angle;

    if (duration_ms == 0 || angle == servo->current_angle) {
        apply_angle_pwm(servo, angle);
        servo->moving = false;
        notify_update(servo);
        return;
    }

    if (ensure_move_timer(servo) != ESP_OK) {
        apply_angle_pwm(servo, angle);
        notify_update(servo);
        return;
    }

    servo->move_start_angle = servo->current_angle;
    servo->move_start_us = esp_timer_get_time();
    servo->move_duration_ms = duration_ms;
    servo->moving = true;
    esp_timer_start_periodic(servo->move_timer, SERVO_MOVE_TICK_US);
    notify_update(servo);
}

int Servo_read(Servo_t* servo) {
    return (servo && servo->attached) ? servo->current_angle : 0;
}

int Servo_target(Servo_t* servo) {
    if (!servo || !servo->attached) {
        return 0;
    }
    return servo->target_angle;
}

bool Servo_attached(Servo_t* servo) {
    return servo && servo->attached;
}

bool Servo_isMoving(Servo_t* servo) {
    return servo && servo->attached && servo->moving;
}

void Servo_setAngleRange(Servo_t* servo, int min_angle, int max_angle) {
    if (!servo || !servo->attached || max_angle <= min_angle) {
        return;
    }
    stop_move_timer(servo);
    servo->min_angle = min_angle;
    servo->max_angle = max_angle;
    if (servo->current_angle < min_angle) {
        servo->current_angle = min_angle;
    } else if (servo->current_angle > max_angle) {
        servo->current_angle = max_angle;
    }
    servo->target_angle = servo->current_angle;
    apply_angle_pwm(servo, servo->current_angle);
}

void Servo_setType(Servo_t* servo, servo_type_t type) {
    if (!servo || !servo->attached) {
        return;
    }
    stop_move_timer(servo);
    apply_type_range(servo, type);
    if (servo->current_angle < servo->min_angle) {
        servo->current_angle = servo->min_angle;
    } else if (servo->current_angle > servo->max_angle) {
        servo->current_angle = servo->max_angle;
    }
    servo->target_angle = servo->current_angle;
    apply_angle_pwm(servo, servo->current_angle);
    notify_update(servo);
}

void Servo_setUpdateCallback(Servo_t* servo, ServoUpdateCb cb, void* ctx) {
    if (!servo) {
        return;
    }
    servo->on_update = cb;
    servo->on_update_ctx = ctx;
}

#else

mcpwm_timer_handle_t g_servo_timer = NULL;
bool g_servo_timer_initialized = false;

esp_err_t Servo_attach(Servo_t* servo, int gpio_pin, servo_type_t type) {
    (void)servo;
    (void)gpio_pin;
    (void)type;
    return ESP_ERR_NOT_SUPPORTED;
}

void Servo_detach(Servo_t* servo) { (void)servo; }
void Servo_write(Servo_t* servo, int angle) {
    (void)servo;
    (void)angle;
}
void Servo_writeTimed(Servo_t* servo, int angle, uint32_t duration_ms) {
    (void)servo;
    (void)angle;
    (void)duration_ms;
}
int Servo_read(Servo_t* servo) {
    (void)servo;
    return 0;
}
int Servo_target(Servo_t* servo) {
    (void)servo;
    return 0;
}
bool Servo_attached(Servo_t* servo) {
    (void)servo;
    return false;
}
bool Servo_isMoving(Servo_t* servo) {
    (void)servo;
    return false;
}
void Servo_setAngleRange(Servo_t* servo, int min_angle, int max_angle) {
    (void)servo;
    (void)min_angle;
    (void)max_angle;
}
void Servo_setType(Servo_t* servo, servo_type_t type) {
    (void)servo;
    (void)type;
}
void Servo_setUpdateCallback(Servo_t* servo, ServoUpdateCb cb, void* ctx) {
    (void)servo;
    (void)cb;
    (void)ctx;
}

#endif
