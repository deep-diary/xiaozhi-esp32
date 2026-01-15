/*
 * Servo.c - ESP32 舵机控制类实现
 */

#include "Servo.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "Servo";

// 全局定时器（所有舵机共享）
mcpwm_timer_handle_t g_servo_timer = NULL;
bool g_servo_timer_initialized = false;

// 角度转换为PWM比较值
static inline uint32_t angle_to_compare(int angle, int min_angle, int max_angle) {
    // 限制角度范围
    if (angle < min_angle) angle = min_angle;
    if (angle > max_angle) angle = max_angle;
    
    return (angle - min_angle) * (SERVO_MAX_PULSEWIDTH_US - SERVO_MIN_PULSEWIDTH_US) / 
           (max_angle - min_angle) + SERVO_MIN_PULSEWIDTH_US;
}

// 初始化全局舵机定时器
esp_err_t init_servo_timer(void) {
    if (g_servo_timer_initialized) {
        return ESP_OK;
    }

    // 创建定时器
    mcpwm_timer_config_t timer_config = {
        .group_id = 0,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = SERVO_TIMEBASE_RESOLUTION_HZ,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
        .period_ticks = SERVO_TIMEBASE_PERIOD,
        .intr_priority = 0,
        .flags = {
            .update_period_on_empty = false,
            .update_period_on_sync = false,
            .allow_pd = false
        }
    };
    
    esp_err_t ret = mcpwm_new_timer(&timer_config, &g_servo_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create servo timer: %s", esp_err_to_name(ret));
        return ret;
    }

    // 启动定时器
    ret = mcpwm_timer_enable(g_servo_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable servo timer: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = mcpwm_timer_start_stop(g_servo_timer, MCPWM_TIMER_START_NO_STOP);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start servo timer: %s", esp_err_to_name(ret));
        return ret;
    }

    g_servo_timer_initialized = true;
    ESP_LOGI(TAG, "Servo timer initialized");
    return ESP_OK;
}

// 绑定舵机到指定GPIO
esp_err_t Servo_attach(Servo_t *servo, int gpio_pin, servo_type_t type) {
    if (servo == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // 初始化舵机结构
    memset(servo, 0, sizeof(Servo_t));
    servo->gpio_pin = gpio_pin;
    servo->type = type;
    servo->attached = false;

    // 根据类型设置角度范围
    switch (type) {
        case SERVO_TYPE_90:
            servo->min_angle = 0;
            servo->max_angle = 90;
            servo->current_angle = 45;  // 90度舵机默认中间位置
            break;
        case SERVO_TYPE_180:
            servo->min_angle = 0;
            servo->max_angle = 180;
            servo->current_angle = 90;  // 180度舵机默认中间位置
            break;
        case SERVO_TYPE_270:
            servo->min_angle = 0;
            servo->max_angle = 270;
            servo->current_angle = 135; // 270度舵机默认中间位置
            break;
        case SERVO_TYPE_360:
            servo->min_angle = 0;
            servo->max_angle = 360;
            servo->current_angle = 180; // 360度舵机默认中间位置
            break;
        default:
            servo->min_angle = 0;
            servo->max_angle = 180;
            servo->current_angle = 90;  // 默认180度舵机
            break;
    }

    // 初始化全局定时器
    esp_err_t ret = init_servo_timer();
    if (ret != ESP_OK) {
        return ret;
    }

    // 创建操作器
    mcpwm_operator_config_t operator_config = {
        .group_id = 0,
        .intr_priority = 0,
        .flags = {}
    };
    ret = mcpwm_new_operator(&operator_config, &servo->oper);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create operator: %s", esp_err_to_name(ret));
        return ret;
    }

    // 连接操作器到定时器
    ret = mcpwm_operator_connect_timer(servo->oper, g_servo_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect operator to timer: %s", esp_err_to_name(ret));
        mcpwm_del_operator(servo->oper);
        return ret;
    }

    // 创建比较器
    mcpwm_comparator_config_t comparator_config = {
        .flags = {
            .update_cmp_on_tez = true
        }
    };
    ret = mcpwm_new_comparator(servo->oper, &comparator_config, &servo->comparator);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create comparator: %s", esp_err_to_name(ret));
        mcpwm_del_operator(servo->oper);
        return ret;
    }

    // 创建生成器
    mcpwm_generator_config_t generator_config = {
        .gen_gpio_num = gpio_pin,
        .flags = {}
    };
    ret = mcpwm_new_generator(servo->oper, &generator_config, &servo->generator);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create generator: %s", esp_err_to_name(ret));
        mcpwm_del_comparator(servo->comparator);
        mcpwm_del_operator(servo->oper);
        return ret;
    }

    // 设置生成器动作
    ret = mcpwm_generator_set_action_on_timer_event(servo->generator,
        MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set timer event action: %s", esp_err_to_name(ret));
        mcpwm_del_generator(servo->generator);
        mcpwm_del_comparator(servo->comparator);
        mcpwm_del_operator(servo->oper);
        return ret;
    }

    ret = mcpwm_generator_set_action_on_compare_event(servo->generator,
        MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, servo->comparator, MCPWM_GEN_ACTION_LOW));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set compare event action: %s", esp_err_to_name(ret));
        mcpwm_del_generator(servo->generator);
        mcpwm_del_comparator(servo->comparator);
        mcpwm_del_operator(servo->oper);
        return ret;
    }

    // 设置初始角度
    ret = mcpwm_comparator_set_compare_value(servo->comparator, 
        angle_to_compare(servo->current_angle, servo->min_angle, servo->max_angle));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set initial compare value: %s", esp_err_to_name(ret));
        mcpwm_del_generator(servo->generator);
        mcpwm_del_comparator(servo->comparator);
        mcpwm_del_operator(servo->oper);
        return ret;
    }

    servo->attached = true;
    
    // 获取类型描述
    const char* type_desc;
    switch (type) {
        case SERVO_TYPE_90:  type_desc = "90°"; break;
        case SERVO_TYPE_180: type_desc = "180°"; break;
        case SERVO_TYPE_270: type_desc = "270°"; break;
        case SERVO_TYPE_360: type_desc = "360°"; break;
        default: type_desc = "Unknown"; break;
    }
    
    ESP_LOGI(TAG, "Servo attached - GPIO: %d, Type: %s, Range: %d-%d", 
             gpio_pin, type_desc, servo->min_angle, servo->max_angle);
    return ESP_OK;
}

// 分离舵机
void Servo_detach(Servo_t *servo) {
    if (servo == NULL || !servo->attached) {
        return;
    }

    // 清理资源
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
    ESP_LOGI(TAG, "Servo detached from GPIO %d", servo->gpio_pin);
}

// 设置舵机角度
void Servo_write(Servo_t *servo, int angle) {
    if (servo == NULL || !servo->attached) {
        return;
    }

    // 限制角度范围
    if (angle < servo->min_angle) angle = servo->min_angle;
    if (angle > servo->max_angle) angle = servo->max_angle;

    // 设置比较值
    esp_err_t ret = mcpwm_comparator_set_compare_value(servo->comparator, 
        angle_to_compare(angle, servo->min_angle, servo->max_angle));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set servo angle: %s", esp_err_to_name(ret));
        return;
    }

    servo->current_angle = angle;
    ESP_LOGD(TAG, "Servo GPIO %d set to %d degrees (range: %d-%d)", 
             servo->gpio_pin, angle, servo->min_angle, servo->max_angle);
}

// 读取当前角度
int Servo_read(Servo_t *servo) {
    if (servo == NULL || !servo->attached) {
        return 0;
    }
    return servo->current_angle;
}

// 检查舵机是否已绑定
bool Servo_attached(Servo_t *servo) {
    if (servo == NULL) {
        return false;
    }
    return servo->attached;
}

// 设置自定义角度范围
void Servo_setAngleRange(Servo_t *servo, int min_angle, int max_angle) {
    if (servo == NULL || !servo->attached) {
        return;
    }

    servo->min_angle = min_angle;
    servo->max_angle = max_angle;
    
    // 确保当前角度在新范围内
    if (servo->current_angle < min_angle) {
        servo->current_angle = min_angle;
    } else if (servo->current_angle > max_angle) {
        servo->current_angle = max_angle;
    }
    
    // 更新舵机位置
    Servo_write(servo, servo->current_angle);
    
    ESP_LOGI(TAG, "Servo angle range updated to %d-%d", min_angle, max_angle);
}

// 舵机测试函数 - 在指定时间内来回摆动
void Servo_testSweep(Servo_t *servo, uint32_t duration_ms, int step_size) {
    if (servo == NULL || !servo->attached) {
        return;
    }

    // 为每个舵机使用独立的状态变量
    static uint32_t last_move_time[16] = {0}; // 支持最多16个舵机
    static int direction[16] = {1}; // 1=增加, -1=减少
    static bool initialized[16] = {false};
    static int servo_count = 0; // 舵机计数器
    static int servo_gpio_map[16] = {-1}; // GPIO引脚映射表
    
    // 查找或分配舵机索引
    int servo_index = -1;
    
    // 首先查找是否已经分配过这个GPIO
    for (int i = 0; i < servo_count; i++) {
        if (servo_gpio_map[i] == servo->gpio_pin) {
            servo_index = i;
            break;
        }
    }
    
    // 如果没有找到，分配新的索引
    if (servo_index == -1) {
        if (servo_count < 16) {
            servo_index = servo_count;
            servo_gpio_map[servo_index] = servo->gpio_pin;
            servo_count++;
        } else {
            // 如果超过最大数量，使用模运算分配到现有槽位
            servo_index = servo->gpio_pin % 16;
        }
    }
    
    // 初始化该舵机的状态变量
    if (!initialized[servo_index]) {
        last_move_time[servo_index] = esp_timer_get_time() / 1000;
        direction[servo_index] = 1;
        initialized[servo_index] = true;
    }

    uint32_t current_time = esp_timer_get_time() / 1000;
    
    // 计算移动间隔 (总时间 / 总步数)
    int total_steps = (servo->max_angle - servo->min_angle) / step_size;
    uint32_t move_interval = duration_ms / (total_steps * 2); // 来回摆动，所以乘以2
    
    if (current_time - last_move_time[servo_index] >= move_interval) {
        last_move_time[servo_index] = current_time;
        
        // 更新角度
        servo->current_angle += direction[servo_index] * step_size;
        
        // 检查边界，改变方向
        if (servo->current_angle >= servo->max_angle) {
            servo->current_angle = servo->max_angle;
            direction[servo_index] = -1; // 开始减少
        } else if (servo->current_angle <= servo->min_angle) {
            servo->current_angle = servo->min_angle;
            direction[servo_index] = 1;  // 开始增加
        }
        
        // 设置舵机角度
        Servo_write(servo, servo->current_angle);
        
        // 每10度打印一次状态
        if (servo->current_angle % 10 == 0) {
            ESP_LOGI(TAG, "Servo test sweep: %d degrees (range: %d-%d)", 
                     servo->current_angle, servo->min_angle, servo->max_angle);
        }
    }
}
