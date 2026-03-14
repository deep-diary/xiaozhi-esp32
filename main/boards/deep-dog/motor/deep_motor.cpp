#include "deep_motor.h"
#include <esp_log.h>

static const char* TAG = "DeepMotor";

DeepMotor::DeepMotor(CircularStrip* led_strip) : active_motor_id_(-1), registered_count_(0),
                         teaching_mode_(false), teaching_data_ready_(false),
                         teaching_point_count_(0), current_execute_index_(0),
                         init_status_task_handle_(nullptr), teaching_task_handle_(nullptr),
                         execute_task_handle_(nullptr), led_state_manager_(nullptr) {
    // 初始化所有电机ID为未注册状态
    for (int i = 0; i < MAX_MOTOR_COUNT; i++) {
        registered_motor_ids_[i] = MOTOR_ID_UNREGISTERED;
        memset(&motor_statuses_[i], 0, sizeof(motor_status_t));
        motor_target_angles_[i] = 0.0f;  // 初始化目标位置为0
    }
    
    // 初始化录制数据数组
    memset(teaching_positions_, 0, sizeof(teaching_positions_));
    
    // 初始化回调函数
    data_callback_ = nullptr;
    callback_user_data_ = nullptr;
    
    // 初始化LED状态管理器
    if (led_strip != nullptr) {
        led_state_manager_ = new DeepMotorLedState(led_strip, this);
        ESP_LOGI(TAG, "LED状态管理器初始化完成");
        
        // 自动启用所有电机的角度指示器（默认启用）
        for (uint8_t motor_id = 1; motor_id <= 6; motor_id++) {
            led_state_manager_->EnableAngleIndicator(motor_id, true);
        }
        ESP_LOGI(TAG, "已自动启用所有电机角度指示器（电机ID: 1-6）");
    } else {
        ESP_LOGI(TAG, "LED功能未启用（led_strip为nullptr）");
    }
    
    ESP_LOGI(TAG, "深度电机管理器初始化完成，最大支持 %d 个电机", MAX_MOTOR_COUNT);
}

DeepMotor::~DeepMotor() {
    // 停止所有任务
    if (init_status_task_handle_ != nullptr) {
        vTaskDelete(init_status_task_handle_);
        init_status_task_handle_ = nullptr;
    }
    
    if (teaching_task_handle_ != nullptr) {
        vTaskDelete(teaching_task_handle_);
        teaching_task_handle_ = nullptr;
    }
    
    if (execute_task_handle_ != nullptr) {
        vTaskDelete(execute_task_handle_);
        execute_task_handle_ = nullptr;
    }
    
    // 清理LED状态管理器
    if (led_state_manager_ != nullptr) {
        delete led_state_manager_;
        led_state_manager_ = nullptr;
    }
    
    ESP_LOGI(TAG, "深度电机管理器析构");
}

bool DeepMotor::processCanFrame(const CanFrame& can_frame) {
    // 检查是否是扩展帧
    if (!can_frame.extd) {
        return false;
    }
    
    // 解析电机ID和主机ID
    uint8_t cmd_type = RX_29ID_DISASSEMBLE_CMD_TYPE(can_frame.identifier);
    // 如果不是反馈帧或者版本查询帧则直接返回，因为其他命令帧解析出来的motor_id是不对的
    if (cmd_type != MOTOR_CMD_FEEDBACK && cmd_type != MOTOR_CMD_VERSION) {
        return false;
    }

    // 解析电机ID和主机ID
    uint8_t motor_id = RX_29ID_DISASSEMBLE_MOTOR_ID(can_frame.identifier);
    uint8_t master_id = RX_29ID_DISASSEMBLE_MASTER_ID(can_frame.identifier);

    
    ESP_LOGI(TAG, "<<<<<<<<<<<收到电机反馈帧: ID=0x%08lX, 电机ID=%d, 主机ID=%d", 
             can_frame.identifier, motor_id, master_id);

    // 查找或注册电机
    int8_t motor_index = findMotorIndex(motor_id);
    if (motor_index == -1) {
        if (!registerMotorId(motor_id)) {
            ESP_LOGE(TAG, "注册电机ID %d 失败，已达到最大数量限制", motor_id);
            return false;
        }
        motor_index = findMotorIndex(motor_id);
        ESP_LOGI(TAG, "新注册电机ID: %d，当前已注册 %d 个电机", motor_id, registered_count_);
        
        // 新电机注册时自动启用角度指示器
        if (led_state_manager_) {
            led_state_manager_->EnableAngleIndicator(motor_id, true);
            ESP_LOGI(TAG, "自动启用新注册电机%d的角度指示器", motor_id);
        }
    }
    
    
    // 解析电机状态数据
    MotorProtocol::parseMotorData(can_frame, &motor_statuses_[motor_index]);

    // 只有反馈帧才更新活跃电机ID和LED状态
    if (cmd_type == MOTOR_CMD_FEEDBACK) {
        // 更新活跃电机ID为当前收到数据的电机
        active_motor_id_ = motor_id;
        
        // 如果在录制模式下，保存位置数据
        if (teaching_mode_ && teaching_point_count_ < MAX_TEACHING_POINTS) {
            teaching_positions_[teaching_point_count_] = motor_statuses_[motor_index].current_angle;
            teaching_point_count_++;
            ESP_LOGI(TAG, "录制数据保存: 点%d, 位置=%.3f rad", 
                     teaching_point_count_, motor_statuses_[motor_index].current_angle);
        }
        
        // 更新LED状态显示
        if (led_state_manager_) {
            DeepMotorLedState::MotorAngleState led_state;
            led_state.current_angle = motor_statuses_[motor_index].current_angle;
            led_state.target_angle = motor_target_angles_[motor_index]; // 使用真实的目标位置
            led_state.is_moving = (motor_statuses_[motor_index].current_speed != 0.0f);
            led_state.is_error = (motor_statuses_[motor_index].error_status != 0);
            led_state.valid_range = {0.0f, 0.0f, false}; // 默认无效范围
            
            // 添加调试日志
            ESP_LOGI(TAG, "更新电机%d LED状态: 角度=%.3f rad (%.1f°), 移动=%s, 错误=%s", 
                     motor_id, 
                     led_state.current_angle, 
                     led_state.current_angle * 180.0f / 3.14159f,
                     led_state.is_moving ? "是" : "否",
                     led_state.is_error ? "是" : "否");
            
            led_state_manager_->SetMotorAngleState(motor_id, led_state);
        }
    }
    
    // 调用回调函数（用于机械臂录制）
    if (data_callback_) {
        data_callback_(motor_id, motor_statuses_[motor_index].current_angle, callback_user_data_);
    }
    
    ESP_LOGD(TAG, "电机 %d 状态更新:\n"
                  "  角度: %.3f rad\n"
                  "  速度: %.3f rad/s\n"
                  "  扭矩: %.3f N·m\n"
                  "  温度: %.1f °C\n"
                  "  主机ID: %d\n"
                  "  错误状态: %d\n"
                  "  霍尔错误: %d\n"
                  "  磁编码错误: %d\n"
                  "  温度错误: %d\n"
                  "  电流错误: %d\n"
                  "  电压错误: %d\n"
                  "  运行模式: %d",
                  motor_id,
                  motor_statuses_[motor_index].current_angle,
                  motor_statuses_[motor_index].current_speed,
                  motor_statuses_[motor_index].current_torque,
                  motor_statuses_[motor_index].current_temp,
                  motor_statuses_[motor_index].master_id,
                  motor_statuses_[motor_index].error_status,
                  motor_statuses_[motor_index].hall_error,
                  motor_statuses_[motor_index].magnet_error,
                  motor_statuses_[motor_index].temp_error,
                  motor_statuses_[motor_index].current_error,
                  motor_statuses_[motor_index].voltage_error,
                  motor_statuses_[motor_index].mode_status);
    
    return true;
}

int8_t DeepMotor::findMotorIndex(uint8_t motor_id) const {
    for (int i = 0; i < MAX_MOTOR_COUNT; i++) {
        if (registered_motor_ids_[i] == motor_id) {
            return i;
        }
    }
    return -1; // 未找到
}

bool DeepMotor::registerMotorId(uint8_t motor_id) {
    // 检查是否已满
    if (registered_count_ >= MAX_MOTOR_COUNT) {
        return false;
    }
    
    // 查找空闲位置
    for (int i = 0; i < MAX_MOTOR_COUNT; i++) {
        if (registered_motor_ids_[i] == MOTOR_ID_UNREGISTERED) {
            registered_motor_ids_[i] = motor_id;
            registered_count_++;
            
            // 初始化电机状态
            memset(&motor_statuses_[i], 0, sizeof(motor_status_t));
            motor_statuses_[i].motor_id = motor_id;
            
            return true;
        }
    }
    
    return false;
}

int8_t DeepMotor::getActiveMotorId() const {
    return active_motor_id_;
}

bool DeepMotor::setActiveMotorId(uint8_t motor_id) {
    if (!isMotorRegistered(motor_id)) {
        ESP_LOGW(TAG, "尝试设置未注册的电机ID %d 为活跃电机", motor_id);
        return false;
    }
    
    active_motor_id_ = motor_id;
    ESP_LOGI(TAG, "设置活跃电机ID为: %d", motor_id);
    return true;
}

bool DeepMotor::getMotorStatus(uint8_t motor_id, motor_status_t* status) const {
    if (status == nullptr) {
        return false;
    }
    
    int8_t motor_index = findMotorIndex(motor_id);
    if (motor_index == -1) {
        ESP_LOGW(TAG, "查询未注册电机ID %d 的状态", motor_id);
        return false;
    }
    
    *status = motor_statuses_[motor_index];
    return true;
}

uint8_t DeepMotor::getRegisteredMotorIds(int8_t* motor_ids, uint8_t max_count) const {
    if (motor_ids == nullptr || max_count == 0) {
        return 0;
    }
    
    uint8_t count = 0;
    for (int i = 0; i < MAX_MOTOR_COUNT && count < max_count; i++) {
        if (registered_motor_ids_[i] != MOTOR_ID_UNREGISTERED) {
            motor_ids[count] = registered_motor_ids_[i];
            count++;
        }
    }
    
    return count;
}

bool DeepMotor::isMotorRegistered(uint8_t motor_id) const {
    return findMotorIndex(motor_id) != -1;
}

uint8_t DeepMotor::getRegisteredCount() const {
    return registered_count_;
}

void DeepMotor::clearAllMotors() {
    for (int i = 0; i < MAX_MOTOR_COUNT; i++) {
        registered_motor_ids_[i] = MOTOR_ID_UNREGISTERED;
        memset(&motor_statuses_[i], 0, sizeof(motor_status_t));
    }
    registered_count_ = 0;
    active_motor_id_ = -1;
    
    ESP_LOGI(TAG, "清除所有电机注册信息");
}

void DeepMotor::printAllMotorStatus() const {
    ESP_LOGI(TAG, "=== 电机状态总览 ===");
    ESP_LOGI(TAG, "已注册电机数量: %d", registered_count_);
    ESP_LOGI(TAG, "当前活跃电机ID: %d", active_motor_id_);
    
    for (int i = 0; i < MAX_MOTOR_COUNT; i++) {
        if (registered_motor_ids_[i] != MOTOR_ID_UNREGISTERED) {
            const motor_status_t* status = &motor_statuses_[i];
            ESP_LOGI(TAG, "电机ID %d: 角度=%.3f rad, 速度=%.3f rad/s, 扭矩=%.3f N·m, 温度=%.1f°C, 模式=%d", 
                     status->motor_id,
                     status->current_angle,
                     status->current_speed,
                     status->current_torque,
                     status->current_temp,
                     status->mode_status);
            
            // 打印错误状态
            if (status->error_status) {
                ESP_LOGW(TAG, "  错误状态: 霍尔=%d, 磁铁=%d, 温度=%d, 电流=%d, 电压=%d",
                         status->hall_error, status->magnet_error, status->temp_error,
                         status->current_error, status->voltage_error);
            }
        }
    }
    ESP_LOGI(TAG, "==================");
}

// 静态任务函数实现
void DeepMotor::initStatusTask(void* parameter) {
    DeepMotor* motor_manager = static_cast<DeepMotor*>(parameter);
    int8_t motor_id = motor_manager->active_motor_id_;
    
    ESP_LOGI(TAG, "初始化状态查询任务启动，电机ID: %d", motor_id);
    
    while (true) {
        if (motor_id != MOTOR_ID_UNREGISTERED) {
            // 发送位置模式查询，获取电机状态
            MotorProtocol::setMotorPositionMode(static_cast<uint8_t>(motor_id));
        }
        vTaskDelay(pdMS_TO_TICKS(INIT_STATUS_RATE_MS));
    }
}

void DeepMotor::recordingTask(void* parameter) {
    DeepMotor* motor_manager = static_cast<DeepMotor*>(parameter);
    int8_t motor_id = motor_manager->active_motor_id_;
    
    ESP_LOGI(TAG, "录制任务启动，电机ID: %d", motor_id);
    
    while (motor_manager->teaching_mode_) {
        if (motor_id != MOTOR_ID_UNREGISTERED) {
            // 发送位置模式查询，获取实时位置
            MotorProtocol::setMotorPositionMode(static_cast<uint8_t>(motor_id));
        }
        vTaskDelay(pdMS_TO_TICKS(TEACHING_SAMPLE_RATE_MS));
    }
    
    ESP_LOGI(TAG, "录制任务结束");
    vTaskDelete(nullptr);
}

void DeepMotor::playTask(void* parameter) {
    DeepMotor* motor_manager = static_cast<DeepMotor*>(parameter);
    int8_t motor_id = motor_manager->active_motor_id_;
    
    ESP_LOGI(TAG, "播放任务启动，电机ID: %d，总点数: %d", motor_id, motor_manager->teaching_point_count_);
    
    for (uint16_t i = 0; i < motor_manager->teaching_point_count_; i++) {
        float position = motor_manager->teaching_positions_[i];
        
        // 发送位置指令
        // MotorProtocol::setPosition(static_cast<uint8_t>(motor_id), position, 30.0f);
        MotorProtocol::setPositionOnly(static_cast<uint8_t>(motor_id), position);
        
        ESP_LOGD(TAG, "播放录制点 %d/%d: 位置=%.3f rad", 
                 i + 1, motor_manager->teaching_point_count_, position);
        
        vTaskDelay(pdMS_TO_TICKS(TEACHING_SAMPLE_RATE_MS));
    }
    
    ESP_LOGI(TAG, "播放录制完成");
    vTaskDelete(nullptr);
}

// 录制功能实现
bool DeepMotor::startTeaching(uint8_t motor_id) {
    if (teaching_mode_) {
        ESP_LOGW(TAG, "录制模式已启动，请先停止当前录制");
        return false;
    }
    
    if (!isMotorRegistered(motor_id)) {
        ESP_LOGE(TAG, "电机ID %d 未注册，无法开始录制", motor_id);
        return false;
    }
    
    ESP_LOGI(TAG, "开始录制模式，电机ID: %d", motor_id);
    
    // 1. 停止电机（便于人工拖动）
    if (!MotorProtocol::resetMotor(motor_id)) {
        ESP_LOGE(TAG, "停止电机失败");
        return false;
    }
    
    // 2. 设置录制标志位
    teaching_mode_ = true;
    teaching_data_ready_ = false;
    teaching_point_count_ = 0;
    current_execute_index_ = 0;
    
    // 3. 清空录制数据数组
    memset(teaching_positions_, 0, sizeof(teaching_positions_));
    
    // 4. 设置活跃电机ID
    setActiveMotorId(motor_id);
    
    // 5. 创建录制任务
    BaseType_t ret = xTaskCreate(recordingTask, "recording_task", 4096, this, 5, &teaching_task_handle_);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建录制任务失败");
        teaching_mode_ = false;
        return false;
    }
    
    ESP_LOGI(TAG, "录制模式启动成功，可以开始拖动电机");
    return true;
}

bool DeepMotor::stopTeaching() {
    if (!teaching_mode_) {
        ESP_LOGW(TAG, "录制模式未启动");
        return false;
    }
    
    ESP_LOGI(TAG, "结束录制模式，共记录 %d 个录制点", teaching_point_count_);
    
    // 1. 终止录制任务
    if (teaching_task_handle_ != nullptr) {
        vTaskDelete(teaching_task_handle_);
        teaching_task_handle_ = nullptr;
    }
    
    // 2. 清除录制标志位
    teaching_mode_ = false;
    
    // 3. 保存录制数据
    if (teaching_point_count_ > 0) {
        teaching_data_ready_ = true;
        ESP_LOGI(TAG, "录制数据保存完成，共 %d 个点", teaching_point_count_);
    } else {
        ESP_LOGW(TAG, "录制数据为空");
    }
    
    return true;
}

bool DeepMotor::executeTeaching(uint8_t motor_id) {
    if (!teaching_data_ready_) {
        ESP_LOGE(TAG, "录制数据未就绪，请先完成录制");
        return false;
    }
    
    if (teaching_point_count_ == 0) {
        ESP_LOGE(TAG, "录制数据为空，无法播放");
        return false;
    }
    
    if (!isMotorRegistered(motor_id)) {
        ESP_LOGE(TAG, "电机ID %d 未注册，无法播放录制", motor_id);
        return false;
    }
    
    ESP_LOGI(TAG, "开始播放录制，电机ID: %d，总点数: %d", motor_id, teaching_point_count_);
    
    // 1. 使能电机
    if (!MotorProtocol::enableMotor(motor_id)) {
        ESP_LOGE(TAG, "使能电机失败");
        return false;
    }
    
    // 2. 设置活跃电机ID
    setActiveMotorId(motor_id);
    
    // 3. 创建播放任务
    BaseType_t ret = xTaskCreate(playTask, "play_task", 4096, this, 5, &execute_task_handle_);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建播放任务失败");
        return false;
    }
    
    ESP_LOGI(TAG, "播放录制任务启动成功");
    return true;
}

bool DeepMotor::isTeachingMode() const {
    return teaching_mode_;
}

bool DeepMotor::isTeachingDataReady() const {
    return teaching_data_ready_;
}

uint16_t DeepMotor::getTeachingPointCount() const {
    return teaching_point_count_;
}

bool DeepMotor::startInitStatusTask(uint8_t motor_id) {
    if (init_status_task_handle_ != nullptr) {
        ESP_LOGW(TAG, "初始化状态查询任务已存在，请先停止");
        return false;
    }
    
    if (!isMotorRegistered(motor_id)) {
        ESP_LOGE(TAG, "电机ID %d 未注册，无法启动状态查询任务", motor_id);
        return false;
    }
    
    // 设置活跃电机ID
    setActiveMotorId(motor_id);
    
    // 创建初始化状态查询任务
    BaseType_t ret = xTaskCreate(initStatusTask, "init_status_task", 4096, this, 3, &init_status_task_handle_);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建初始化状态查询任务失败");
        return false;
    }
    
    ESP_LOGI(TAG, "初始化状态查询任务启动成功，电机ID: %d", motor_id);
    return true;
}

bool DeepMotor::stopInitStatusTask() {
    if (init_status_task_handle_ == nullptr) {
        ESP_LOGW(TAG, "初始化状态查询任务不存在");
        return false;
    }
    
    vTaskDelete(init_status_task_handle_);
    init_status_task_handle_ = nullptr;
    
    ESP_LOGI(TAG, "初始化状态查询任务已停止");
    return true;
}

void DeepMotor::setMotorDataCallback(MotorDataCallback callback, void* user_data) {
    data_callback_ = callback;
    callback_user_data_ = user_data;
    ESP_LOGI(TAG, "电机数据回调函数设置完成");
}

// ========== LED控制接口实现 ==========

bool DeepMotor::enableAngleIndicator(uint8_t motor_id, bool enabled) {
    if (!led_state_manager_) {
        ESP_LOGW(TAG, "LED状态管理器未初始化");
        return false;
    }
    
    if (!isMotorRegistered(motor_id)) {
        ESP_LOGW(TAG, "电机ID %d 未注册", motor_id);
        return false;
    }
    
    led_state_manager_->EnableAngleIndicator(motor_id, enabled);
    ESP_LOGI(TAG, "%s电机%d角度指示器", enabled ? "启用" : "禁用", motor_id);
    return true;
}

bool DeepMotor::disableAngleIndicator(uint8_t motor_id) {
    return enableAngleIndicator(motor_id, false);
}

bool DeepMotor::setAngleRange(uint8_t motor_id, float min_angle, float max_angle) {
    if (!led_state_manager_) {
        ESP_LOGW(TAG, "LED状态管理器未初始化");
        return false;
    }
    
    if (!isMotorRegistered(motor_id)) {
        ESP_LOGW(TAG, "电机ID %d 未注册", motor_id);
        return false;
    }
    
    led_state_manager_->SetAngleRange(motor_id, min_angle, max_angle);
    ESP_LOGI(TAG, "设置电机%d角度范围: [%.2f, %.2f] rad", motor_id, min_angle, max_angle);
    return true;
}

DeepMotorLedState::MotorAngleState DeepMotor::getAngleStatus(uint8_t motor_id) const {
    if (!led_state_manager_) {
        ESP_LOGW(TAG, "LED状态管理器未初始化");
        return {0.0f, 0.0f, false, false, {0.0f, 0.0f, false}};
    }
    
    return led_state_manager_->GetMotorAngleState(motor_id);
}

DeepMotorLedState::AngleRange DeepMotor::getAngleRange(uint8_t motor_id) const {
    if (!led_state_manager_) {
        ESP_LOGW(TAG, "LED状态管理器未初始化");
        return {0.0f, 0.0f, false};
    }
    
    return led_state_manager_->GetAngleRange(motor_id);
}

bool DeepMotor::isAngleIndicatorEnabled(uint8_t motor_id) const {
    if (!led_state_manager_) {
        ESP_LOGW(TAG, "LED状态管理器未初始化");
        return false;
    }
    
    return led_state_manager_->IsAngleIndicatorEnabled(motor_id);
}

void DeepMotor::stopAllAngleIndicators() {
    if (!led_state_manager_) {
        ESP_LOGW(TAG, "LED状态管理器未初始化");
        return;
    }
    
    led_state_manager_->StopCurrentEffect();
    ESP_LOGI(TAG, "停止所有角度指示器");
}

DeepMotorLedState* DeepMotor::getLedStateManager() const {
    return led_state_manager_;
}

// ========== 呼吸灯控制接口实现 ==========

bool DeepMotor::enableBreatheEffect(uint8_t motor_id, uint8_t red, uint8_t green, uint8_t blue) {
    if (!led_state_manager_) {
        ESP_LOGW(TAG, "LED状态管理器未初始化");
        return false;
    }
    
    StripColor color = {red, green, blue};
    bool result = led_state_manager_->EnableBreatheEffect(motor_id, color);
    
    if (result) {
        ESP_LOGI(TAG, "启用电机%d呼吸灯效果成功", motor_id);
    } else {
        ESP_LOGW(TAG, "启用电机%d呼吸灯效果失败", motor_id);
    }
    
    return result;
}

bool DeepMotor::disableBreatheEffect(uint8_t motor_id) {
    if (!led_state_manager_) {
        ESP_LOGW(TAG, "LED状态管理器未初始化");
        return false;
    }
    
    bool result = led_state_manager_->DisableBreatheEffect(motor_id);
    
    if (result) {
        ESP_LOGI(TAG, "禁用电机%d呼吸灯效果成功", motor_id);
    } else {
        ESP_LOGW(TAG, "禁用电机%d呼吸灯效果失败", motor_id);
    }
    
    return result;
}

bool DeepMotor::isBreatheEffectEnabled(uint8_t motor_id) const {
    if (!led_state_manager_) {
        return false;
    }
    
    return led_state_manager_->IsBreatheEffectEnabled(motor_id);
}

// ========== 软件版本查询接口实现 ==========

bool DeepMotor::getMotorSoftwareVersion(uint8_t motor_id, char* version, size_t buffer_size) const {
    if (version == nullptr || buffer_size == 0) {
        ESP_LOGW(TAG, "版本号缓冲区为空或大小为0");
        return false;
    }
    
    int8_t motor_index = findMotorIndex(motor_id);
    if (motor_index == -1) {
        ESP_LOGW(TAG, "查询未注册电机ID %d 的软件版本", motor_id);
        return false;
    }
    
    // 复制版本号字符串
    strncpy(version, motor_statuses_[motor_index].version, buffer_size - 1);
    version[buffer_size - 1] = '\0'; // 确保字符串以null结尾
    
    ESP_LOGI(TAG, "电机ID %d 软件版本: %s", motor_id, version);
    return true;
}

bool DeepMotor::setMotorTargetAngle(uint8_t motor_id, float target_angle) {
    int8_t motor_index = findMotorIndex(motor_id);
    if (motor_index == -1) {
        ESP_LOGW(TAG, "设置未注册电机ID %d 的目标位置", motor_id);
        return false;
    }
    
    motor_target_angles_[motor_index] = target_angle;
    ESP_LOGI(TAG, "设置电机ID %d 目标位置: %.3f rad (%.1f°)", 
             motor_id, target_angle, target_angle * 180.0f / 3.14159f);
    return true;
}

bool DeepMotor::getMotorTargetAngle(uint8_t motor_id, float* target_angle) const {
    if (target_angle == nullptr) {
        ESP_LOGW(TAG, "目标位置输出指针为空");
        return false;
    }
    
    int8_t motor_index = findMotorIndex(motor_id);
    if (motor_index == -1) {
        ESP_LOGW(TAG, "查询未注册电机ID %d 的目标位置", motor_id);
        return false;
    }
    
    *target_angle = motor_target_angles_[motor_index];
    ESP_LOGI(TAG, "电机ID %d 目标位置: %.3f rad (%.1f°)", 
             motor_id, *target_angle, *target_angle * 180.0f / 3.14159f);
    return true;
}

bool DeepMotor::setMotorPosition(uint8_t motor_id, float position, float max_speed) {
    // 先更新目标位置
    if (!setMotorTargetAngle(motor_id, position)) {
        ESP_LOGW(TAG, "设置电机ID %d 目标位置失败", motor_id);
        return false;
    }
    
    // 调用协议层方法设置电机位置
    if (!MotorProtocol::setPosition(motor_id, position, max_speed)) {
        ESP_LOGW(TAG, "发送电机ID %d 位置指令失败", motor_id);
        return false;
    }
    
    ESP_LOGI(TAG, "设置电机ID %d 位置: %.3f rad (%.1f°), 最大速度: %.1f rad/s", 
             motor_id, position, position * 180.0f / 3.14159f, max_speed);
    return true;
}
