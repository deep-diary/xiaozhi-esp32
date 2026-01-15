/*
 * Gimbal.c - ESP32 云台控制类实现
 */

#include "Gimbal.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "Gimbal";

// 初始化云台
esp_err_t Gimbal_init(Gimbal_t *gimbal, int pan_gpio, int tilt_gpio) {
    if (gimbal == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // 初始化云台结构
    memset(gimbal, 0, sizeof(Gimbal_t));
    gimbal->initialized = false;

    // 初始化水平舵机 (270度舵机)
    esp_err_t ret = Servo_attach(&gimbal->pan_servo, pan_gpio, SERVO_TYPE_270);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize pan servo on GPIO %d", pan_gpio);
        return ret;
    }

    // 初始化垂直舵机 (180度舵机)
    ret = Servo_attach(&gimbal->tilt_servo, tilt_gpio, SERVO_TYPE_180);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize tilt servo on GPIO %d", tilt_gpio);
        Servo_detach(&gimbal->pan_servo);
        return ret;
    }

    gimbal->initialized = true;
    ESP_LOGI(TAG, "Gimbal initialized - Pan: GPIO%d (0-270°), Tilt: GPIO%d (0-180°)", 
             pan_gpio, tilt_gpio);
    return ESP_OK;
}

// 反初始化云台
void Gimbal_deinit(Gimbal_t *gimbal) {
    if (gimbal == NULL || !gimbal->initialized) {
        return;
    }

    Servo_detach(&gimbal->pan_servo);
    Servo_detach(&gimbal->tilt_servo);
    gimbal->initialized = false;
    ESP_LOGI(TAG, "Gimbal deinitialized");
}

// 同时设置两个舵机角度
void Gimbal_setAngles(Gimbal_t *gimbal, int pan_angle, int tilt_angle) {
    if (gimbal == NULL || !gimbal->initialized) {
        return;
    }

    Servo_write(&gimbal->pan_servo, pan_angle);
    Servo_write(&gimbal->tilt_servo, tilt_angle);
    
    ESP_LOGI(TAG, "Gimbal angles set - Pan: %d°, Tilt: %d°", pan_angle, tilt_angle);
}

// 获取两个舵机角度
void Gimbal_getAngles(Gimbal_t *gimbal, int *pan_angle, int *tilt_angle) {
    if (gimbal == NULL || !gimbal->initialized || pan_angle == NULL || tilt_angle == NULL) {
        return;
    }

    *pan_angle = Servo_read(&gimbal->pan_servo);
    *tilt_angle = Servo_read(&gimbal->tilt_servo);
}

// 设置水平舵机角度
void Gimbal_setPanAngle(Gimbal_t *gimbal, int angle) {
    if (gimbal == NULL || !gimbal->initialized) {
        return;
    }
    Servo_write(&gimbal->pan_servo, angle);
}

// 设置垂直舵机角度
void Gimbal_setTiltAngle(Gimbal_t *gimbal, int angle) {
    if (gimbal == NULL || !gimbal->initialized) {
        return;
    }
    Servo_write(&gimbal->tilt_servo, angle);
}

// 获取水平舵机角度
int Gimbal_getPanAngle(Gimbal_t *gimbal) {
    if (gimbal == NULL || !gimbal->initialized) {
        return 0;
    }
    return Servo_read(&gimbal->pan_servo);
}

// 获取垂直舵机角度
int Gimbal_getTiltAngle(Gimbal_t *gimbal) {
    if (gimbal == NULL || !gimbal->initialized) {
        return 0;
    }
    return Servo_read(&gimbal->tilt_servo);
}

// 检查云台是否已初始化
bool Gimbal_isInitialized(Gimbal_t *gimbal) {
    if (gimbal == NULL) {
        return false;
    }
    return gimbal->initialized;
}

// 云台方向控制函数
void Gimbal_moveUp(Gimbal_t *gimbal, int angle) {
    if (gimbal == NULL || !gimbal->initialized) {
        return;
    }
    
    int current_tilt = Gimbal_getTiltAngle(gimbal);
    int new_tilt = current_tilt + angle;
    
    // 限制在0-180度范围内
    if (new_tilt > 180) new_tilt = 180;
    if (new_tilt < 0) new_tilt = 0;
    
    Gimbal_setTiltAngle(gimbal, new_tilt);
    ESP_LOGI(TAG, "Gimbal moved up: %d° -> %d°", current_tilt, new_tilt);
}

void Gimbal_moveDown(Gimbal_t *gimbal, int angle) {
    if (gimbal == NULL || !gimbal->initialized) {
        return;
    }
    
    int current_tilt = Gimbal_getTiltAngle(gimbal);
    int new_tilt = current_tilt - angle;
    
    // 限制在0-180度范围内
    if (new_tilt > 180) new_tilt = 180;
    if (new_tilt < 0) new_tilt = 0;
    
    Gimbal_setTiltAngle(gimbal, new_tilt);
    ESP_LOGI(TAG, "Gimbal moved down: %d° -> %d°", current_tilt, new_tilt);
}

void Gimbal_moveLeft(Gimbal_t *gimbal, int angle) {
    if (gimbal == NULL || !gimbal->initialized) {
        return;
    }
    
    int current_pan = Gimbal_getPanAngle(gimbal);
    int new_pan = current_pan - angle;
    
    // 限制在0-270度范围内
    if (new_pan > 270) new_pan = 270;
    if (new_pan < 0) new_pan = 0;
    
    Gimbal_setPanAngle(gimbal, new_pan);
    ESP_LOGI(TAG, "Gimbal moved left: %d° -> %d°", current_pan, new_pan);
}

void Gimbal_moveRight(Gimbal_t *gimbal, int angle) {
    if (gimbal == NULL || !gimbal->initialized) {
        return;
    }
    
    int current_pan = Gimbal_getPanAngle(gimbal);
    int new_pan = current_pan + angle;
    
    // 限制在0-270度范围内
    if (new_pan > 270) new_pan = 270;
    if (new_pan < 0) new_pan = 0;
    
    Gimbal_setPanAngle(gimbal, new_pan);
    ESP_LOGI(TAG, "Gimbal moved right: %d° -> %d°", current_pan, new_pan);
}

// 云台测试函数 - 两个舵机同时摆动
void Gimbal_testSweep(Gimbal_t *gimbal, uint32_t duration_ms, int step_size) {
    if (gimbal == NULL || !gimbal->initialized) {
        return;
    }

    // 同时测试两个舵机
    Servo_testSweep(&gimbal->pan_servo, duration_ms, step_size);
    Servo_testSweep(&gimbal->tilt_servo, duration_ms, step_size);
}

// 只测试水平舵机摆动
void Gimbal_testPanSweep(Gimbal_t *gimbal, uint32_t duration_ms, int step_size) {
    if (gimbal == NULL || !gimbal->initialized) {
        return;
    }
    Servo_testSweep(&gimbal->pan_servo, duration_ms, step_size);
}

// 只测试垂直舵机摆动
void Gimbal_testTiltSweep(Gimbal_t *gimbal, uint32_t duration_ms, int step_size) {
    if (gimbal == NULL || !gimbal->initialized) {
        return;
    }
    Servo_testSweep(&gimbal->tilt_servo, duration_ms, step_size);
}

// 交替测试两个舵机摆动
void Gimbal_testBothSweep(Gimbal_t *gimbal, uint32_t duration_ms, int step_size) {
    if (gimbal == NULL || !gimbal->initialized) {
        return;
    }

    static uint32_t last_switch_time = 0;
    static bool testing_pan = true;
    uint32_t current_time = esp_timer_get_time() / 1000;
    
    // 每5秒切换测试的舵机
    if (current_time - last_switch_time >= 5000) {
        last_switch_time = current_time;
        testing_pan = !testing_pan;
        ESP_LOGI(TAG, "Switching to test %s servo", testing_pan ? "pan" : "tilt");
    }
    
    if (testing_pan) {
        Servo_testSweep(&gimbal->pan_servo, duration_ms, step_size);
    } else {
        Servo_testSweep(&gimbal->tilt_servo, duration_ms, step_size);
    }
}

// CAN舵机控制处理函数
void Gimbal_handleCanCommand(Gimbal_t *gimbal, const CanFrame* frame) {
    if (!gimbal || !frame) {
        ESP_LOGE(TAG, "Gimbal_handleCanCommand: 无效参数");
        return;
    }
    
    if (!Gimbal_isInitialized(gimbal)) {
        ESP_LOGW(TAG, "Gimbal_handleCanCommand: 云台未初始化");
        return;
    }
    
    if (frame->data_length_code < 4) {
        ESP_LOGW(TAG, "Gimbal_handleCanCommand: CAN帧数据长度不足");
        return;
    }
    
    uint8_t servo_id = frame->data[0];
    uint8_t command = frame->data[1];
    uint16_t angle = (frame->data[2] << 8) | frame->data[3];
    
    ESP_LOGI(TAG, "舵机控制命令 - ID: %d, 命令: %d, 角度: %d", servo_id, command, angle);
    
    switch(command) {
        case 1: // 水平舵机控制
            if (servo_id == 1) {
                Gimbal_setAngles(gimbal, angle, -1); // -1表示不改变垂直角度
                ESP_LOGI(TAG, "设置水平舵机角度: %d度", angle);
            }
            break;
        case 2: // 垂直舵机控制
            if (servo_id == 2) {
                Gimbal_setAngles(gimbal, -1, angle); // -1表示不改变水平角度
                ESP_LOGI(TAG, "设置垂直舵机角度: %d度", angle);
            }
            break;
        case 3: // 同时控制两个舵机
            if (servo_id == 0) {
                uint16_t tilt_angle = (frame->data_length_code >= 6) ? 
                    ((frame->data[4] << 8) | frame->data[5]) : 90;
                Gimbal_setAngles(gimbal, angle, tilt_angle);
                ESP_LOGI(TAG, "设置云台角度 - 水平: %d度, 垂直: %d度", angle, tilt_angle);
            }
            break;
        default:
            ESP_LOGW(TAG, "未知的舵机控制命令: %d", command);
            break;
    }
}
