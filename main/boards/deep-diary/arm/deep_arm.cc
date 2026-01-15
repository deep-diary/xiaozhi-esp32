#include "deep_arm.h"
#include "esp_log.h"

static const char* TAG = "DeepArm";

DeepArm::DeepArm(DeepMotor* deep_motor, const uint8_t motor_ids[ARM_MOTOR_COUNT]) 
    : deep_motor_(deep_motor), settings_(nullptr) {
    
    // 复制电机ID数组
    memcpy(motor_ids_, motor_ids, sizeof(motor_ids_));
    
    // 初始化状态
    memset(&arm_status_, 0, sizeof(arm_status_t));
    
    // 初始化轨迹规划器
    memset(&trajectory_planner_, 0, sizeof(trajectory_planner_t));
    memset(&trajectory_config_, 0, sizeof(trajectory_config_t));
    
    // 设置默认轨迹规划参数
    for (int i = 0; i < ARM_MOTOR_COUNT; i++) {
        trajectory_config_.max_velocity[i] = 2.0f;      // 2 rad/s
        trajectory_config_.max_acceleration[i] = 5.0f;   // 5 rad/s²
        trajectory_config_.max_jerk[i] = 10.0f;          // 10 rad/s³
        
        // 初始化电机位置数据
        current_motor_positions_[i] = 0.0f;
        motor_position_valid_[i] = false;
    }
    trajectory_config_.interpolation_factor = INTERPOLATION_FACTOR;
    
    trajectory_planner_init(&trajectory_planner_);
    trajectory_planner_init(&move_to_first_planner_);
    arm_status_.boundary_status = BOUNDARY_NOT_CALIBRATED;
    arm_status_.has_error = false;
    arm_status_.error_code = 0;
    
    // 初始化录制数据
    memset(recording_positions_, 0, sizeof(recording_positions_));
    current_recording_index_ = 0;
    recording_start_time_ = 0;
    collected_motors_ = 0;
    recording_query_count_ = 0;
    recording_stop_time_ = 0;
    
    // 初始化边界数据
    memset(&arm_boundary_, 0, sizeof(arm_boundary_t));
    arm_boundary_.is_calibrated = false;
    
    // 初始化任务句柄
    status_query_task_handle_ = nullptr;
    boundary_query_task_handle_ = nullptr;
    recording_task_handle_ = nullptr;
    play_task_handle_ = nullptr;
    
    // 初始化播放控制
    play_stop_requested_ = false;
    
    // 初始化设置存储
    settings_ = new Settings("deep_arm", true);
    
    // 加载边界数据
    loadBoundaryData();
    
    // 设置电机数据回调函数
    if (deep_motor_) {
        deep_motor_->setMotorDataCallback([](uint8_t motor_id, float position, void* user_data) {
            DeepArm* arm = static_cast<DeepArm*>(user_data);
            if (!arm) return;
            
            // 找到电机在数组中的索引
            int motor_index = -1;
            for (int i = 0; i < ARM_MOTOR_COUNT; i++) {
                if (arm->motor_ids_[i] == motor_id) {
                    motor_index = i;
                    break;
                }
            }
            
            if (motor_index < 0) return;
            
            // 更新最新电机位置数据（始终更新，不管是否在录制）
            arm->current_motor_positions_[motor_index] = position;
            arm->motor_position_valid_[motor_index] = true;
            
            // 录制模式处理
            if (arm->arm_status_.is_recording && arm->arm_status_.recording_point_count < MAX_RECORDING_POINTS) {
                // 保存位置数据
                arm->recording_positions_[arm->current_recording_index_].positions[motor_index] = position;
                
                // 标记该电机数据已收集
                arm->collected_motors_ |= (1 << motor_index);
                
                // 计算相对时间
                uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
                uint32_t relative_time = current_time - arm->recording_start_time_;
                
                // 调试日志：记录每个电机的数据收集情况（减少日志频率）
                if (arm->collected_motors_ == 0x3F || arm->collected_motors_ == 0) {
                    ESP_LOGI(TAG, "录制数据: 电机%d(索引%d), 位置=%.3f, 已收集电机=0x%02X, 相对时间=%dms", 
                             motor_id, motor_index, position, arm->collected_motors_, relative_time);
                } else {
                    ESP_LOGD(TAG, "录制数据: 电机%d(索引%d), 位置=%.3f, 已收集电机=0x%02X, 相对时间=%dms", 
                             motor_id, motor_index, position, arm->collected_motors_, relative_time);
                }
                
                // 如果所有电机数据都收集到了，完成当前录制点
                if (arm->collected_motors_ == 0x3F) { // 所有6个电机都收集到了 (0x3F = 0b111111)
                    arm->arm_status_.recording_point_count++;
                    arm->current_recording_index_++;
                    arm->collected_motors_ = 0; // 重置
                    
                    ESP_LOGI(TAG, "录制点 %d 完成, 相对时间=%dms, 当前总点数: %d", 
                             arm->arm_status_.recording_point_count, relative_time, arm->arm_status_.recording_point_count);
                } else {
                    // 调试：显示还缺少哪些电机（减少日志频率）
                    uint8_t missing_motors = 0x3F & ~arm->collected_motors_;
                    ESP_LOGD(TAG, "等待更多电机数据，缺少电机位掩码: 0x%02X, 当前收集: 0x%02X", missing_motors, arm->collected_motors_);
                }
            }
            
            // 边界标定模式处理
            if (arm->arm_status_.boundary_status == BOUNDARY_CALIBRATING) {
                // 更新边界数据
                if (position < arm->arm_boundary_.min_positions[motor_index]) {
                    arm->arm_boundary_.min_positions[motor_index] = position;
                }
                if (position > arm->arm_boundary_.max_positions[motor_index]) {
                    arm->arm_boundary_.max_positions[motor_index] = position;
                }
                
                ESP_LOGD(TAG, "电机%d边界更新: [%.3f, %.3f]", 
                         motor_id, arm->arm_boundary_.min_positions[motor_index], 
                         arm->arm_boundary_.max_positions[motor_index]);
            }
        }, this);
    }
    
    ESP_LOGI(TAG, "机械臂控制器初始化完成，电机ID: %d,%d,%d,%d,%d,%d", 
             motor_ids_[0], motor_ids_[1], motor_ids_[2], 
             motor_ids_[3], motor_ids_[4], motor_ids_[5]);
}

DeepArm::~DeepArm() {
    // 停止所有任务
    stopStatusQueryTask();
    stopBoundaryCalibration();
    stopRecording();
    
    // 保存边界数据
    saveBoundaryData();
    
    // 清理资源
    if (settings_) {
        delete settings_;
        settings_ = nullptr;
    }
    
    ESP_LOGI(TAG, "机械臂控制器析构完成");
}

bool DeepArm::setZeroPosition() {
    ESP_LOGI(TAG, "设置机械臂零位");
    
    bool all_success = true;
    for (int i = 0; i < ARM_MOTOR_COUNT; i++) {
        if (!MotorProtocol::setMotorZero(motor_ids_[i])) {
            ESP_LOGE(TAG, "设置电机%d零位失败", motor_ids_[i]);
            all_success = false;
        } else {
            ESP_LOGI(TAG, "电机%d零位设置成功", motor_ids_[i]);
        }
    }
    
    if (all_success) {
        ESP_LOGI(TAG, "机械臂零位设置完成");
    } else {
        ESP_LOGE(TAG, "机械臂零位设置部分失败");
    }
    
    return all_success;
}

bool DeepArm::enableArm() {
    ESP_LOGI(TAG, "启动机械臂");
    
    // 检查是否已初始化
    if (!arm_status_.is_initialized) {
        ESP_LOGE(TAG, "机械臂未初始化，请先初始化");
        return false;
    }
    
    bool all_success = true;
    for (int i = 0; i < ARM_MOTOR_COUNT; i++) {
        if (!MotorProtocol::enableMotor(motor_ids_[i])) {
            ESP_LOGE(TAG, "启动电机%d失败", motor_ids_[i]);
            all_success = false;
        } else {
            ESP_LOGI(TAG, "电机%d启动成功", motor_ids_[i]);
        }
    }
    
    if (all_success) {
        arm_status_.is_enabled = true;
        ESP_LOGI(TAG, "机械臂启动完成");
    } else {
        ESP_LOGE(TAG, "机械臂启动部分失败");
    }
    
    return all_success;
}

bool DeepArm::disableArm() {
    ESP_LOGI(TAG, "关闭机械臂");
    
    bool all_success = true;
    for (int i = 0; i < ARM_MOTOR_COUNT; i++) {
        if (!MotorProtocol::resetMotor(motor_ids_[i])) {
            ESP_LOGE(TAG, "关闭电机%d失败", motor_ids_[i]);
            all_success = false;
        } else {
            ESP_LOGI(TAG, "电机%d关闭成功", motor_ids_[i]);
        }
    }
    
    if (all_success) {
        arm_status_.is_enabled = false;
        ESP_LOGI(TAG, "机械臂关闭完成");
    } else {
        ESP_LOGE(TAG, "机械臂关闭部分失败");
    }
    
    return all_success;
}

bool DeepArm::initializeArm(const float max_speeds[ARM_MOTOR_COUNT]) {
    ESP_LOGI(TAG, "初始化机械臂");
    
    // 打印当前状态用于调试
    ESP_LOGI(TAG, "当前状态 - 已初始化: %s, 已启动: %s, 录制中: %s", 
             arm_status_.is_initialized ? "是" : "否",
             arm_status_.is_enabled ? "是" : "否", 
             arm_status_.is_recording ? "是" : "否");
    
    // 检查是否正在录制
    if (arm_status_.is_recording) {
        ESP_LOGE(TAG, "无法初始化：正在录制中，请先停止录制");
        return false;
    }
    
    // 检查是否已启动（只有在未启动时才能初始化）
    if (arm_status_.is_enabled) {
        ESP_LOGE(TAG, "无法初始化：机械臂已启动，请先关闭机械臂");
        return false;
    }
    
    // 默认最大速度
    float default_speeds[ARM_MOTOR_COUNT] = {30.0f, 30.0f, 30.0f, 30.0f, 30.0f, 30.0f};
    const float* speeds = max_speeds ? max_speeds : default_speeds;
    
    bool all_success = true;
    for (int i = 0; i < ARM_MOTOR_COUNT; i++) {
        if (!MotorProtocol::initializeMotor(motor_ids_[i], speeds[i])) {
            ESP_LOGE(TAG, "初始化电机%d失败", motor_ids_[i]);
            all_success = false;
        } else {
            ESP_LOGI(TAG, "电机%d初始化成功，最大速度: %.1f rad/s", motor_ids_[i], speeds[i]);
        }
    }
    
    
    if (all_success) {
        arm_status_.is_initialized = true;
        ESP_LOGI(TAG, "机械臂初始化完成");
    } else {
        ESP_LOGE(TAG, "机械臂初始化部分失败");
    }

    // 启动状态查询任务
    if (!startStatusQueryTask()) {
        ESP_LOGE(TAG, "启动机械臂状态查询任务失败");
        all_success = false;
    }
    
    return all_success;
}

bool DeepArm::startRecording() {
    if (arm_status_.is_recording) {
        ESP_LOGW(TAG, "录制模式已启动，请先停止当前录制");
        return false;
    }
    
    // 检查是否已初始化
    if (!arm_status_.is_initialized) {
        ESP_LOGE(TAG, "机械臂未初始化，请先初始化");
        return false;
    }
    
    // 如果录制任务句柄存在，检查并重置
    if (recording_task_handle_ != nullptr) {
        // 检查任务是否真的还在运行
        eTaskState task_state = eTaskGetState(recording_task_handle_);
        if (task_state == eDeleted || task_state == eInvalid) {
            // 任务已经结束，重置句柄
            ESP_LOGI(TAG, "检测到录制任务已结束，重置句柄");
            recording_task_handle_ = nullptr;
        } else {
            ESP_LOGW(TAG, "录制任务正在运行，请稍后再试");
            return false;
        }
    }
    
    ESP_LOGI(TAG, "开始机械臂录制模式");
    
    // 1. 停止所有电机（便于人工拖动）
    if (!disableArm()) {
        ESP_LOGE(TAG, "停止机械臂失败");
        return false;
    }
    // 延时0.5s，防止发送的停止电机的指令后，马上录制标志位置位，那这点的数据，也是会被录制进去
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // 2. 设置录制标志位
    arm_status_.is_recording = true;
    arm_status_.recording_data_ready = false;
    arm_status_.recording_point_count = 0;
    current_recording_index_ = 0;
    collected_motors_ = 0;
    recording_query_count_ = 0;
    
    // 3. 记录录制开始时间
    recording_start_time_ = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    // 4. 清空录制数据数组
    memset(recording_positions_, 0, sizeof(recording_positions_));
    
    // 4. 创建录制任务
    BaseType_t ret = xTaskCreate(recordingTask, "arm_recording_task", 4096, this, 8, &recording_task_handle_);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建录制任务失败");
        arm_status_.is_recording = false;
        return false;
    }
    
    ESP_LOGI(TAG, "机械臂录制模式启动成功，可以开始拖动机械臂");
    return true;
}

bool DeepArm::stopRecording() {
    if (!arm_status_.is_recording) {
        ESP_LOGW(TAG, "录制模式未启动");
        return false;
    }
    
    ESP_LOGI(TAG, "结束机械臂录制模式，当前记录 %d 个录制点，已发送 %d 个查询指令", 
             arm_status_.recording_point_count, recording_query_count_);
    
    // 1. 记录停止时间
    recording_stop_time_ = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    // 2. 计算期望的点数（基于最大录制时间）
    uint32_t total_time = recording_stop_time_ - recording_start_time_;
    uint32_t expected_points = MAX_RECORDING_TIME_MS / RECORDING_SAMPLE_RATE_MS;
    
    ESP_LOGI(TAG, "录制时间: %dms, 期望点数: %d, 实际点数: %d", 
             total_time, expected_points, arm_status_.recording_point_count);
    
    // 3. 等待逻辑：等待延迟的CAN响应数据
    ESP_LOGI(TAG, "开始等待延迟数据，等待时间: %dms", RECORDING_WAIT_THRESHOLD_MS);
    uint32_t wait_start_time = recording_stop_time_;
    uint32_t initial_points = arm_status_.recording_point_count;
    uint32_t last_point_count = initial_points;
    uint32_t no_data_count = 0; // 连续无数据计数
    
    while (true) {
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        uint32_t wait_time = current_time - wait_start_time;
        uint32_t current_points = arm_status_.recording_point_count;
        
        // 检查是否有新数据
        if (current_points > last_point_count) {
            last_point_count = current_points;
            no_data_count = 0; // 重置无数据计数
        } else {
            no_data_count++;
        }
        
        // 退出条件1：等待时间达到阈值
        if (wait_time >= RECORDING_WAIT_THRESHOLD_MS) {
            ESP_LOGW(TAG, "等待完成 - 录制时间: %dms, 等待时间: %dms, 初始点数: %d, 最终点数: %d, 期望点数: %d", 
                     total_time, wait_time, initial_points, current_points, expected_points);
            break;
        }
        
        // 退出条件2：达到期望点数
        if (current_points >= expected_points) {
            ESP_LOGI(TAG, "达到期望点数，提前结束等待 - 当前点数: %d, 期望点数: %d", 
                     current_points, expected_points);
            break;
        }
        
        // 退出条件3：连续多次无新数据（连续5次检查无数据，即500ms无数据）
        if (no_data_count >= 5) {
            ESP_LOGI(TAG, "无新数据，提前结束等待 - 等待时间: %dms, 当前点数: %d", 
                     wait_time, current_points);
            break;
        }
        
        // 每500ms打印一次等待状态
        if (wait_time % 500 == 0) {
            ESP_LOGI(TAG, "等待延迟数据... 等待时间: %dms, 当前点数: %d, 无数据计数: %d", 
                     wait_time, current_points, no_data_count);
        }
        
        vTaskDelay(pdMS_TO_TICKS(100)); // 等待100ms后再次检查
    }
    
    // 4. 保存录制数据（在终止任务之前）
    uint16_t final_point_count = arm_status_.recording_point_count;  // 保存当前点数
    ESP_LOGI(TAG, "准备保存录制数据，当前录制点数: %d", final_point_count);
    if (final_point_count > 0) {
        arm_status_.recording_data_ready = true;
        
        // 计算最终录制总时间
        uint32_t final_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        uint32_t total_recording_time = final_time - recording_start_time_;
        
        ESP_LOGI(TAG, "机械臂录制数据保存完成，共 %d 个点，总时间 %dms，发送指令数 %d", 
                 final_point_count, total_recording_time, recording_query_count_);
        
        // 打印所有录制点的详细信息
        ESP_LOGI(TAG, "=== 录制点详细信息 ===");
        for (uint16_t i = 0; i < final_point_count; i++) {
            ESP_LOGI(TAG, "录制点 %d: [%.3f, %.3f, %.3f, %.3f, %.3f, %.3f]", 
                     i + 1,
                     recording_positions_[i].positions[0],
                     recording_positions_[i].positions[1], 
                     recording_positions_[i].positions[2],
                     recording_positions_[i].positions[3],
                     recording_positions_[i].positions[4],
                     recording_positions_[i].positions[5]);
        }
        ESP_LOGI(TAG, "======================");
    } else {
        ESP_LOGW(TAG, "机械臂录制数据为空");
    }
    
    // 5. 使能机械臂（录制结束后恢复控制）
    ESP_LOGI(TAG, "录制结束，使能机械臂");
    if (!enableArm()) {
        ESP_LOGW(TAG, "录制结束后使能机械臂失败");
    }
    
    // 6. 清除录制标志位
    arm_status_.is_recording = false;
    
    // 7. 重置录制任务句柄（任务会自己删除自己）
    recording_task_handle_ = nullptr;
    
    return true;
}

bool DeepArm::playRecording(uint32_t loop_count) {
    if (!arm_status_.recording_data_ready) {
        ESP_LOGE(TAG, "录制数据未就绪，请先完成录制");
        return false;
    }
    
    if (arm_status_.recording_point_count == 0) {
        ESP_LOGE(TAG, "录制数据为空，无法播放");
        return false;
    }
    
    // 检查是否已初始化
    if (!arm_status_.is_initialized) {
        ESP_LOGE(TAG, "机械臂未初始化，请先初始化");
        return false;
    }
    
    // 检查是否正在播放
    if (play_task_handle_ != nullptr) {
        ESP_LOGW(TAG, "播放任务正在运行，请先停止当前播放");
        return false;
    }
    
    ESP_LOGI(TAG, "开始播放机械臂录制，总点数: %d", arm_status_.recording_point_count);
    
    // 1. 使能机械臂
    if (!enableArm()) {
        ESP_LOGE(TAG, "使能机械臂失败");
        return false;
    }
    
    // 2. 播放前准备：移动到第一点的逻辑已移到循环播放中
    ESP_LOGI(TAG, "播放前准备：轨迹插值处理");
    
    // 3. 对录制轨迹进行插值处理
    ESP_LOGI(TAG, "对录制轨迹进行插值处理，插值倍数: %d", INTERPOLATION_FACTOR);
    
    // 将录制数据转换为轨迹点格式
    trajectory_point_t* recorded_points = (trajectory_point_t*)malloc(arm_status_.recording_point_count * sizeof(trajectory_point_t));
    if (!recorded_points) {
        ESP_LOGE(TAG, "内存分配失败，无法进行轨迹插值");
        return false;
    }
    
    for (uint16_t i = 0; i < arm_status_.recording_point_count; i++) {
        for (int j = 0; j < ARM_MOTOR_COUNT; j++) {
            recorded_points[i].positions[j] = recording_positions_[i].positions[j];
            recorded_points[i].velocities[j] = 0.0f; // 录制时没有速度信息
        }
        recorded_points[i].time_ms = i * RECORDING_SAMPLE_RATE_MS; // 假设均匀时间间隔
    }
    
    // 使用三次样条插值（固定插值倍数）
    uint16_t target_points = arm_status_.recording_point_count * INTERPOLATION_FACTOR; // 使用宏定义插值倍数
    if (target_points > MAX_TRAJECTORY_POINTS) {
        target_points = MAX_TRAJECTORY_POINTS;
    }
    
    if (!trajectory_cubic_spline_interpolate(&trajectory_planner_, recorded_points, 
                                           arm_status_.recording_point_count, target_points)) {
        ESP_LOGW(TAG, "轨迹插值失败，使用原始轨迹进行简单插值");
        
        // 插值失败时，使用简单的线性插值作为备选方案
        if (!trajectory_interpolate(&trajectory_planner_, recorded_points, 
                                  arm_status_.recording_point_count, target_points)) {
            ESP_LOGE(TAG, "简单插值也失败，无法播放录制");
            free(recorded_points);
            return false;
        }
        
        ESP_LOGI(TAG, "使用简单插值：原始 %d 点 -> 插值 %d 点", 
                 arm_status_.recording_point_count, trajectory_planner_.point_count);
        free(recorded_points);
    } else {
        ESP_LOGI(TAG, "轨迹插值成功：原始 %d 点 -> 插值 %d 点", 
                 arm_status_.recording_point_count, trajectory_planner_.point_count);
        
        // 打印插值后的轨迹点用于验证
        // trajectory_print_points(&trajectory_planner_, 0); // 打印全部轨迹点
        free(recorded_points);
    }
    
    // 4. 创建播放任务（传递循环次数）
    struct PlayTaskParams {
        DeepArm* arm;
        uint32_t loop_count;
    };
    
    PlayTaskParams* params = new PlayTaskParams{this, loop_count};
    BaseType_t ret = xTaskCreate([](void* p) {
        PlayTaskParams* params = static_cast<PlayTaskParams*>(p);
        DeepArm* arm = params->arm;
        uint32_t loop_count = params->loop_count;
        delete params; // 释放内存
        
        // 执行循环播放
        for (uint32_t loop = 0; loop_count == 0 || loop < loop_count; loop++) {
            // 检查停止请求
            if (arm->play_stop_requested_) {
                ESP_LOGI(TAG, "收到停止请求，终止播放");
                break;
            }
            
            if (loop_count > 0) {
                ESP_LOGI(TAG, "开始第 %d/%d 次循环播放", loop + 1, loop_count);
            } else {
                ESP_LOGI(TAG, "开始第 %d 次循环播放（无限循环）", loop + 1);
            }
            
            // 每次循环播放前，先平滑移动到第一点
            if (ENABLE_TRAJECTORY_EXECUTION) {
                ESP_LOGW(TAG, "🔄 循环播放准备：从当前位置平滑移动到第一点");
                if (!arm->moveToFirstPoint()) {
                    ESP_LOGW(TAG, "移动到第一点失败，继续播放");
                }
            } else {
                ESP_LOGI(TAG, "轨迹验证模式：跳过移动到第一点");
            }
            
            // 执行单次播放
            arm->executeSinglePlay();
            
            // 再次检查停止请求
            if (arm->play_stop_requested_) {
                ESP_LOGI(TAG, "收到停止请求，终止播放");
                break;
            }
            
            if (loop_count > 0 && loop < loop_count - 1) {
                ESP_LOGI(TAG, "第 %d 次循环播放完成，准备下一次", loop + 1);
                vTaskDelay(pdMS_TO_TICKS(1000)); // 循环间隔1秒
            }
        }
        
        ESP_LOGI(TAG, "所有循环播放完成");
        
        // 清理播放任务句柄
        arm->play_task_handle_ = nullptr;
        vTaskDelete(nullptr);
    }, "arm_play_task", 4096, params, 5, &play_task_handle_);
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建播放任务失败");
        delete params;
        return false;
    }
    
    ESP_LOGI(TAG, "机械臂播放任务启动成功");
    return true;
}

bool DeepArm::stopPlayback() {
    if (play_task_handle_ == nullptr) {
        ESP_LOGW(TAG, "没有正在播放的任务");
        return false;
    }
    
    // 设置停止请求标志
    play_stop_requested_ = true;
    ESP_LOGI(TAG, "已请求停止播放，等待任务结束...");
    
    // 等待任务结束（最多等待5秒）
    uint32_t timeout_ms = 5000;
    uint32_t start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    while (play_task_handle_ != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(100)); // 等待100ms
        
        uint32_t elapsed = xTaskGetTickCount() * portTICK_PERIOD_MS - start_time;
        if (elapsed > timeout_ms) {
            ESP_LOGW(TAG, "停止播放超时，强制终止任务");
            vTaskDelete(play_task_handle_);
            play_task_handle_ = nullptr;
            break;
        }
    }
    
    // 重置停止请求标志
    play_stop_requested_ = false;
    
    ESP_LOGI(TAG, "播放已停止");
    return true;
}

bool DeepArm::isPlaying() const {
    return (play_task_handle_ != nullptr);
}

bool DeepArm::moveToPosition(const float target_positions[ARM_MOTOR_COUNT]) {
    if (!target_positions) {
        ESP_LOGE(TAG, "目标位置数组为空");
        return false;
    }
    
    // 设置移动状态标志
    arm_status_.is_moving = true;
    
    // 获取当前位置
    trajectory_point_t start_point;
    trajectory_point_t end_point;
    
    // 获取当前电机位置（使用异步更新的位置数据）
    float current_positions[ARM_MOTOR_COUNT];
    if (getCurrentMotorPositions(current_positions)) {
        for (int i = 0; i < ARM_MOTOR_COUNT; i++) {
            start_point.positions[i] = current_positions[i];
            start_point.velocities[i] = 0.0f; // 假设当前速度为0
        }
    } else {
        ESP_LOGW(TAG, "无法获取当前电机位置，使用默认值");
        for (int i = 0; i < ARM_MOTOR_COUNT; i++) {
            start_point.positions[i] = 0.0f;
            start_point.velocities[i] = 0.0f;
        }
    }
    
    // 设置目标位置
    for (int i = 0; i < ARM_MOTOR_COUNT; i++) {
        end_point.positions[i] = target_positions[i];
        end_point.velocities[i] = 0.0f; // 目标速度为0
    }
    
    start_point.time_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    end_point.time_ms = start_point.time_ms + POINT_TO_POINT_DURATION_MS; // 使用宏定义时长
    
    // 规划轨迹 - 使用独立的轨迹规划器，避免覆盖录制轨迹数据
    if (!trajectory_plan_point_to_point(&move_to_first_planner_, &start_point, &end_point, &trajectory_config_)) {
        ESP_LOGE(TAG, "轨迹规划失败");
        return false;
    }
    
    ESP_LOGW(TAG, "⚠️ 开始执行回到第一点的轨迹规划，总点数: %d", move_to_first_planner_.point_count);
    
    if (ENABLE_TRAJECTORY_EXECUTION) {
        ESP_LOGI(TAG, "轨迹执行模式：实际控制电机");
        
        // 执行轨迹
        trajectory_point_t point;
        uint32_t last_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        uint16_t point_index = 0;
        
        // 计算并打印延时间隔信息
        uint32_t calculated_interval;
        if (move_to_first_planner_.point_count > 1) {
            calculated_interval = POINT_TO_POINT_DURATION_MS / (move_to_first_planner_.point_count - 1);
            ESP_LOGI(TAG, "轨迹执行延时计算（点对点）：总时长=%dms, 点数=%d, 计算间隔=%dms", 
                     POINT_TO_POINT_DURATION_MS, move_to_first_planner_.point_count, calculated_interval);
        } else {
            calculated_interval = TRAJECTORY_SAMPLE_INTERVAL_MS / INTERPOLATION_FACTOR;
            ESP_LOGI(TAG, "轨迹执行延时计算（插值轨迹）：原始间隔=%dms, 插值倍数=%d, 计算间隔=%dms", 
                     TRAJECTORY_SAMPLE_INTERVAL_MS, INTERPOLATION_FACTOR, calculated_interval);
        }
        
        while (trajectory_get_next_point(&move_to_first_planner_, &point)) {
            // 设置电机位置
            bool all_success = true;
            for (int i = 0; i < ARM_MOTOR_COUNT; i++) {
                if (!MotorProtocol::setPositionOnly(motor_ids_[i], point.positions[i])) {
                    ESP_LOGW(TAG, "设置电机%d位置失败", motor_ids_[i]);
                    all_success = false;
                }
            }
            
            if (!all_success) {
                ESP_LOGW(TAG, "部分电机位置设置失败");
            }
            
            // 获取实际位置并计算误差
            float actual_positions[ARM_MOTOR_COUNT];
            if (getCurrentMotorPositions(actual_positions)) {
                // 计算误差并打印
                float errors[ARM_MOTOR_COUNT];
                for (int i = 0; i < ARM_MOTOR_COUNT; i++) {
                    errors[i] = actual_positions[i] - point.positions[i];
                }
                
                ESP_LOGW(TAG, "🔄 轨迹点 %d/%d: 目标=[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f] 误差=[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f]", 
                         point_index + 1, move_to_first_planner_.point_count,
                         point.positions[0], point.positions[1], point.positions[2],
                         point.positions[3], point.positions[4], point.positions[5],
                         errors[0], errors[1], errors[2],
                         errors[3], errors[4], errors[5]);
            } else {
                ESP_LOGD(TAG, "轨迹点 %d/%d (位置数据无效)", point_index + 1, trajectory_planner_.point_count);
            }
            
            point_index++;
            
            // 等待到下一个时间点
            uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            uint32_t elapsed = current_time - last_time;
            
            // 根据轨迹点数和总时长计算正确的延时间隔
            uint32_t target_interval;
            if (move_to_first_planner_.point_count > 1) {
                // 对于点对点轨迹，使用POINT_TO_POINT_DURATION_MS计算间隔
                target_interval = POINT_TO_POINT_DURATION_MS / (move_to_first_planner_.point_count - 1);
            } else {
                // 对于其他轨迹（如插值后的录制轨迹），需要考虑插值倍数
                // 原始采样间隔除以插值倍数，保持总时长一致
                target_interval = TRAJECTORY_SAMPLE_INTERVAL_MS / INTERPOLATION_FACTOR;
            }
            
            if (elapsed < target_interval) {
                vTaskDelay(pdMS_TO_TICKS(target_interval - elapsed));
            }
            
            last_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        }
        
        ESP_LOGI(TAG, "轨迹执行完成");
    } else {
        ESP_LOGI(TAG, "轨迹验证模式：仅打印轨迹点，不控制电机");
        ESP_LOGI(TAG, "轨迹规划完成，共 %d 个点", trajectory_planner_.point_count);
    }
    
    // 清除移动状态标志
    arm_status_.is_moving = false;
    
    return true;
}

bool DeepArm::moveToFirstPoint() {
    if (arm_status_.recording_point_count == 0) {
        ESP_LOGW(TAG, "没有录制数据，无法移动到第一点");
        return false;
    }
    
    ESP_LOGI(TAG, "从最后一点平滑移动到第一点");
    
    // 获取第一点的位置
    float first_point_positions[ARM_MOTOR_COUNT];
    for (int i = 0; i < ARM_MOTOR_COUNT; i++) {
        first_point_positions[i] = recording_positions_[0].positions[i];
    }
    
    return moveToPosition(first_point_positions);
}

bool DeepArm::startStatusQueryTask() {
    if (status_query_task_handle_ != nullptr) {
        ESP_LOGW(TAG, "状态查询任务已启动");
        return false;
    }
    
    BaseType_t ret = xTaskCreate(statusQueryTask, "arm_status_query", 4096, this, 3, &status_query_task_handle_);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建状态查询任务失败");
        return false;
    }
    
    ESP_LOGI(TAG, "机械臂状态查询任务启动成功");
    return true;
}

bool DeepArm::stopStatusQueryTask() {
    if (status_query_task_handle_ == nullptr) {
        ESP_LOGW(TAG, "状态查询任务未启动");
        return false;
    }
    
    vTaskDelete(status_query_task_handle_);
    status_query_task_handle_ = nullptr;
    
    ESP_LOGI(TAG, "机械臂状态查询任务停止成功");
    return true;
}

bool DeepArm::startBoundaryCalibration() {
    if (arm_status_.boundary_status == BOUNDARY_CALIBRATING) {
        ESP_LOGW(TAG, "边界标定已在进行中");
        return false;
    }
    
    ESP_LOGI(TAG, "开始边界位置标定");
    
    // 1. 停止机械臂
    if (!disableArm()) {
        ESP_LOGE(TAG, "停止机械臂失败");
        return false;
    }
    
    // 2. 设置标定状态
    arm_status_.boundary_status = BOUNDARY_CALIBRATING;
    
    // 3. 初始化边界数据
    for (int i = 0; i < ARM_MOTOR_COUNT; i++) {
        arm_boundary_.min_positions[i] = 999.0f;  // 初始化为大值
        arm_boundary_.max_positions[i] = -999.0f; // 初始化为小值
    }
    
    // 4. 创建边界查询任务
    BaseType_t ret = xTaskCreate(boundaryQueryTask, "arm_boundary_query", 4096, this, 4, &boundary_query_task_handle_);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建边界查询任务失败");
        arm_status_.boundary_status = BOUNDARY_NOT_CALIBRATED;
        return false;
    }
    
    ESP_LOGI(TAG, "边界位置标定启动成功，请依次转动每个关节");
    return true;
}

bool DeepArm::stopBoundaryCalibration() {
    if (arm_status_.boundary_status != BOUNDARY_CALIBRATING) {
        ESP_LOGW(TAG, "边界标定未在进行中");
        return false;
    }
    
    ESP_LOGI(TAG, "结束边界位置标定");
    
    // 1. 终止边界查询任务
    if (boundary_query_task_handle_ != nullptr) {
        vTaskDelete(boundary_query_task_handle_);
        boundary_query_task_handle_ = nullptr;
    }
    
    // 2. 设置标定完成状态
    arm_status_.boundary_status = BOUNDARY_CALIBRATED;
    arm_boundary_.is_calibrated = true;
    
    // 3. 保存边界数据
    saveBoundaryData();
    
    // 4. 打印边界数据
    printBoundaryData();
    
    ESP_LOGI(TAG, "边界位置标定完成");
    return true;
}

bool DeepArm::setArmPosition(const float positions[ARM_MOTOR_COUNT], const float max_speeds[ARM_MOTOR_COUNT]) {
    // 检查边界是否已标定
    if (!arm_boundary_.is_calibrated) {
        ESP_LOGE(TAG, "边界未标定，无法进行位置控制");
        return false;
    }
    
    // 创建位置结构体进行边界检查
    arm_position_t position;
    memcpy(position.positions, positions, sizeof(position.positions));
    
    if (!checkPositionLimits(position)) {
        ESP_LOGE(TAG, "位置超出边界限制");
        return false;
    }
    
    ESP_LOGI(TAG, "设置机械臂位置");
    
    bool all_success = true;
    for (int i = 0; i < ARM_MOTOR_COUNT; i++) {
        bool success;
        if (max_speeds) {
            success = MotorProtocol::setPosition(motor_ids_[i], positions[i], max_speeds[i]);
        } else {
            success = MotorProtocol::setPositionOnly(motor_ids_[i], positions[i]);
        }
        
        if (!success) {
            ESP_LOGE(TAG, "设置电机%d位置失败", motor_ids_[i]);
            all_success = false;
        } else {
            ESP_LOGD(TAG, "电机%d位置设置成功: %.3f rad", motor_ids_[i], positions[i]);
        }
    }
    
    if (all_success) {
        ESP_LOGI(TAG, "机械臂位置设置完成");
    } else {
        ESP_LOGE(TAG, "机械臂位置设置部分失败");
    }
    
    return all_success;
}

bool DeepArm::getArmStatus(arm_status_t* status) const {
    if (!status) {
        return false;
    }
    
    memcpy(status, &arm_status_, sizeof(arm_status_t));
    return true;
}

bool DeepArm::getArmBoundary(arm_boundary_t* boundary) const {
    if (!boundary) {
        return false;
    }
    
    memcpy(boundary, &arm_boundary_, sizeof(arm_boundary_t));
    return true;
}

bool DeepArm::getCurrentMotorPositions(float positions[ARM_MOTOR_COUNT]) const {
    if (!positions) {
        return false;
    }
    
    // 检查所有电机位置数据是否有效
    bool all_valid = true;
    for (int i = 0; i < ARM_MOTOR_COUNT; i++) {
        if (!motor_position_valid_[i]) {
            all_valid = false;
            break;
        }
    }
    
    if (!all_valid) {
        return false;
    }
    
    // 复制位置数据
    memcpy(positions, current_motor_positions_, sizeof(current_motor_positions_));
    return true;
}

bool DeepArm::areAllMotorsEnabled() const {
    for (int i = 0; i < ARM_MOTOR_COUNT; i++) {
        motor_status_t status;
        if (deep_motor_->getMotorStatus(motor_ids_[i], &status)) {
            if (status.mode_status == MOTOR_MODE_RESET) {
                ESP_LOGD(TAG, "电机%d未启动（模式：复位）", motor_ids_[i]);
                return false;
            }
        } else {
            ESP_LOGD(TAG, "无法获取电机%d状态", motor_ids_[i]);
            return false;
        }
    }
    return true;
}

bool DeepArm::canSafelyInitialize() const {
    // 检查所有电机是否都未启动（模式为复位状态）
    for (int i = 0; i < ARM_MOTOR_COUNT; i++) {
        motor_status_t status;
        if (deep_motor_->getMotorStatus(motor_ids_[i], &status)) {
            if (status.mode_status != MOTOR_MODE_RESET) {
                ESP_LOGW(TAG, "电机%d已启动（模式：%d），无法安全初始化", motor_ids_[i], status.mode_status);
                return false;
            }
        } else {
            ESP_LOGW(TAG, "无法获取电机%d状态，无法安全初始化", motor_ids_[i]);
            return false;
        }
    }
    return true;
}

bool DeepArm::isRecording() const {
    return arm_status_.is_recording;
}

bool DeepArm::isRecordingDataReady() const {
    return arm_status_.recording_data_ready;
}

uint16_t DeepArm::getRecordingPointCount() const {
    return arm_status_.recording_point_count;
}

bool DeepArm::isBoundaryCalibrated() const {
    return arm_boundary_.is_calibrated;
}

void DeepArm::printArmStatus() const {
    ESP_LOGI(TAG, "=== 机械臂状态 ===");
    ESP_LOGI(TAG, "已初始化: %s", arm_status_.is_initialized ? "是" : "否");
    ESP_LOGI(TAG, "已启动: %s", arm_status_.is_enabled ? "是" : "否");
    ESP_LOGI(TAG, "录制中: %s", arm_status_.is_recording ? "是" : "否");
    ESP_LOGI(TAG, "录制数据就绪: %s", arm_status_.recording_data_ready ? "是" : "否");
    ESP_LOGI(TAG, "录制点数: %d", arm_status_.recording_point_count);
    ESP_LOGI(TAG, "边界标定状态: %d", arm_status_.boundary_status);
    ESP_LOGI(TAG, "边界已标定: %s", arm_boundary_.is_calibrated ? "是" : "否");
    ESP_LOGI(TAG, "==================");
}

// 静态任务函数实现
void DeepArm::statusQueryTask(void* parameter) {
    DeepArm* arm = static_cast<DeepArm*>(parameter);
    
    ESP_LOGI(TAG, "机械臂状态查询任务启动");
    
    while (arm->status_query_task_handle_ != nullptr) {
        // 查询所有电机状态
        for (int i = 0; i < ARM_MOTOR_COUNT; i++) {
            MotorProtocol::setMotorPositionMode(arm->motor_ids_[i]);
        }
        vTaskDelay(pdMS_TO_TICKS(STATUS_QUERY_RATE_MS));
    }
    
    ESP_LOGI(TAG, "机械臂状态查询任务结束");
    vTaskDelete(nullptr);
}

void DeepArm::boundaryQueryTask(void* parameter) {
    DeepArm* arm = static_cast<DeepArm*>(parameter);
    
    ESP_LOGI(TAG, "边界查询任务启动");
    
    while (arm->arm_status_.boundary_status == BOUNDARY_CALIBRATING) {
        // 查询所有电机位置并更新边界
        for (int i = 0; i < ARM_MOTOR_COUNT; i++) {
            MotorProtocol::setMotorPositionMode(arm->motor_ids_[i]);
        }
        vTaskDelay(pdMS_TO_TICKS(BOUNDARY_QUERY_RATE_MS));
    }
    
    ESP_LOGI(TAG, "边界查询任务结束");
    vTaskDelete(nullptr);
}

void DeepArm::recordingTask(void* parameter) {
    DeepArm* arm = static_cast<DeepArm*>(parameter);
    
    ESP_LOGI(TAG, "机械臂录制任务启动，最大录制时间: %dms, 采样间隔: %dms, 最大点数: %d", 
             MAX_RECORDING_TIME_MS, RECORDING_SAMPLE_RATE_MS, MAX_RECORDING_POINTS_CALCULATED);
    
    // 计算期望的查询次数
    uint32_t expected_queries = MAX_RECORDING_TIME_MS / RECORDING_SAMPLE_RATE_MS;
    ESP_LOGI(TAG, "期望查询次数: %d, 期望点数: %d", expected_queries, MAX_RECORDING_POINTS_CALCULATED);
    
    uint32_t task_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    bool should_stop_recording = false;
    
    while (arm->arm_status_.is_recording) {
        // 检查是否超过最大录制时间
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        uint32_t elapsed_time = current_time - task_start_time;
        
        if (elapsed_time >= MAX_RECORDING_TIME_MS) {
            ESP_LOGW(TAG, "录制时间达到最大值 %dms，准备停止录制。当前查询数: %d, 当前点数: %d", 
                     MAX_RECORDING_TIME_MS, arm->recording_query_count_, arm->arm_status_.recording_point_count);
            should_stop_recording = true;
        }
        
        // 检查是否达到最大录制点数
        if (arm->arm_status_.recording_point_count >= MAX_RECORDING_POINTS_CALCULATED) {
            ESP_LOGW(TAG, "录制点数达到最大值 %d，准备停止录制", MAX_RECORDING_POINTS_CALCULATED);
            should_stop_recording = true;
        }
        
        // 如果应该停止录制，调用stopRecording处理等待逻辑
        if (should_stop_recording) {
            ESP_LOGI(TAG, "录制条件达到，调用stopRecording处理等待逻辑。已发送查询数: %d, 录制点数: %d", 
                     arm->recording_query_count_, arm->arm_status_.recording_point_count);
            arm->stopRecording();
            // 注意：stopRecording会清理recording_task_handle_，所以这里直接退出
            vTaskDelete(nullptr);
            return;
        }
        
        // 异步查询所有电机位置（触发电机发送位置数据）
        for (int i = 0; i < ARM_MOTOR_COUNT; i++) {
            MotorProtocol::setMotorPositionMode(arm->motor_ids_[i]);
        }
        
        // 增加查询指令计数
        arm->recording_query_count_++;
        
        // 每10个查询打印一次进度，并添加关键信息
        if (arm->recording_query_count_ % 10 == 0) {
            uint32_t expected_queries = elapsed_time / RECORDING_SAMPLE_RATE_MS;
            ESP_LOGI(TAG, "录制进度: 时间=%dms, 点数=%d, 查询数=%d, 期望查询数=%d", 
                     elapsed_time, arm->arm_status_.recording_point_count, arm->recording_query_count_, expected_queries);
        }
        
        // 记录延迟前后的时间，用于调试
        uint32_t delay_start = xTaskGetTickCount() * portTICK_PERIOD_MS;
        vTaskDelay(pdMS_TO_TICKS(RECORDING_SAMPLE_RATE_MS));
        uint32_t delay_end = xTaskGetTickCount() * portTICK_PERIOD_MS;
        uint32_t actual_delay = delay_end - delay_start;
        
        // 如果延迟时间异常，打印警告
        if (actual_delay > RECORDING_SAMPLE_RATE_MS * 2) {
            ESP_LOGW(TAG, "延迟时间异常: 期望=%dms, 实际=%dms", RECORDING_SAMPLE_RATE_MS, actual_delay);
        }
    }
    
    // 计算最终统计信息
    uint32_t final_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    uint32_t total_time = final_time - task_start_time;
    uint32_t expected_points = MAX_RECORDING_TIME_MS / RECORDING_SAMPLE_RATE_MS; // 基于最大录制时间计算期望点数
    
    // 计算查询效率
    float query_efficiency = (float)arm->recording_query_count_ / expected_points * 100.0f;
    float point_efficiency = (float)arm->arm_status_.recording_point_count / expected_points * 100.0f;
    
    ESP_LOGI(TAG, "机械臂录制任务结束，总时间: %dms, 总查询数: %d, 总点数: %d, 期望点数: %d", 
             total_time, arm->recording_query_count_, arm->arm_status_.recording_point_count, expected_points);
    ESP_LOGI(TAG, "查询效率: %.1f%%, 点数效率: %.1f%%", query_efficiency, point_efficiency);
    
    // 确保录制数据就绪标志位被设置
    if (arm->arm_status_.recording_point_count > 0) {
        arm->arm_status_.recording_data_ready = true;
        ESP_LOGI(TAG, "设置录制数据就绪标志位");
    }
    
    // 清除录制状态标志
    arm->arm_status_.is_recording = false;
    ESP_LOGI(TAG, "清除录制状态标志");
    
    vTaskDelete(nullptr);
}

void DeepArm::playTask(void* parameter) {
    DeepArm* arm = static_cast<DeepArm*>(parameter);
    
    // 使用插值后的轨迹数据
    uint16_t total_points = arm->trajectory_planner_.point_count;
    
    // 验证轨迹数据有效性
    if (total_points == 0) {
        ESP_LOGE(TAG, "轨迹数据为空，播放失败");
        vTaskDelete(nullptr);
        return;
    }
    
    // 设置播放状态标志
    arm->arm_status_.is_playing = true;
    arm->arm_status_.is_moving = true;
    
    ESP_LOGI(TAG, "机械臂播放任务启动，总点数: %d (插值后)", total_points);
    
    // 计算播放延时：按插值倍数缩小延时，保持总时长一致
    uint32_t play_delay_ms = RECORDING_SAMPLE_RATE_MS / INTERPOLATION_FACTOR;
    ESP_LOGI(TAG, "播放延时: %lums (原始: %dms, 插值倍数: %d)", 
             play_delay_ms, RECORDING_SAMPLE_RATE_MS, INTERPOLATION_FACTOR);
    
    if (ENABLE_TRAJECTORY_EXECUTION) {
        ESP_LOGI(TAG, "轨迹执行模式：实际控制电机");
        
        for (uint16_t i = 0; i < total_points; i++) {
            const trajectory_point_t& point = arm->trajectory_planner_.points[i];
            
            // 设置所有电机位置
            bool all_success = true;
            for (int j = 0; j < ARM_MOTOR_COUNT; j++) {
                if (!MotorProtocol::setPositionOnly(arm->motor_ids_[j], point.positions[j])) {
                    ESP_LOGE(TAG, "播放点%d，电机%d位置设置失败", i, arm->motor_ids_[j]);
                    all_success = false;
                }
            }
            
            if (all_success) {
                // 获取实际位置进行对比（使用异步更新的位置数据）
                float actual_positions[ARM_MOTOR_COUNT];
                if (arm->getCurrentMotorPositions(actual_positions)) {
                    // 计算误差并打印
                    float errors[ARM_MOTOR_COUNT];
                    for (int j = 0; j < ARM_MOTOR_COUNT; j++) {
                        errors[j] = actual_positions[j] - point.positions[j];
                    }
                    
                    ESP_LOGW(TAG, "🎬 播放点 %d/%d: 目标=[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f] 误差=[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f]", 
                             i + 1, total_points,
                             point.positions[0], point.positions[1], point.positions[2],
                             point.positions[3], point.positions[4], point.positions[5],
                             errors[0], errors[1], errors[2],
                             errors[3], errors[4], errors[5]);
                } else {
                    ESP_LOGD(TAG, "播放轨迹点 %d/%d (位置数据无效)", i + 1, total_points);
                }
            }
            
            vTaskDelay(pdMS_TO_TICKS(play_delay_ms));
        }
        
        ESP_LOGI(TAG, "机械臂播放录制完成");
        
        // 清除播放和移动状态标志
        arm->arm_status_.is_playing = false;
        arm->arm_status_.is_moving = false;
        
        // 播放结束后，保持最后一个位置，避免跳变
        if (total_points > 0) {
            const trajectory_point_t& last_point = arm->trajectory_planner_.points[total_points - 1];
            ESP_LOGI(TAG, "播放结束，保持最终位置: [%.3f, %.3f, %.3f, %.3f, %.3f, %.3f]",
                     last_point.positions[0], last_point.positions[1], last_point.positions[2],
                     last_point.positions[3], last_point.positions[4], last_point.positions[5]);
            
            // 再次设置最终位置，确保稳定
            for (int j = 0; j < ARM_MOTOR_COUNT; j++) {
                MotorProtocol::setPositionOnly(arm->motor_ids_[j], last_point.positions[j]);
            }
        }
    } else {
        ESP_LOGI(TAG, "轨迹验证模式：仅打印轨迹点，不控制电机");
        
        // 仅打印轨迹点信息
        for (uint16_t i = 0; i < total_points; i++) {
            const trajectory_point_t& point = arm->trajectory_planner_.points[i];
            ESP_LOGI(TAG, "播放点 %d/%d: 位置=[%.3f, %.3f, %.3f, %.3f, %.3f, %.3f]", 
                     i + 1, total_points,
                     point.positions[0], point.positions[1], point.positions[2],
                     point.positions[3], point.positions[4], point.positions[5]);
            
            vTaskDelay(pdMS_TO_TICKS(play_delay_ms));
        }
        
        ESP_LOGI(TAG, "轨迹验证完成");
        
        // 清除播放和移动状态标志
        arm->arm_status_.is_playing = false;
        arm->arm_status_.is_moving = false;
    }
    
    vTaskDelete(nullptr);
}

bool DeepArm::executeSinglePlay() {
    // 使用插值后的轨迹数据
    uint16_t total_points = trajectory_planner_.point_count;
    
    // 验证轨迹数据有效性
    if (total_points == 0) {
        ESP_LOGE(TAG, "轨迹数据为空，播放失败");
        return false;
    }
    
    // 设置播放和移动状态标志
    arm_status_.is_playing = true;
    arm_status_.is_moving = true;
    
    ESP_LOGW(TAG, "🎬 机械臂单次播放开始，总点数: %d (插值后)", total_points);
    
    // 计算播放延时：按插值倍数缩小延时，保持总时长一致
    uint32_t play_delay_ms = RECORDING_SAMPLE_RATE_MS / INTERPOLATION_FACTOR;
    ESP_LOGI(TAG, "播放延时: %lums (原始: %dms, 插值倍数: %d)", 
             play_delay_ms, RECORDING_SAMPLE_RATE_MS, INTERPOLATION_FACTOR);
    
    if (ENABLE_TRAJECTORY_EXECUTION) {
        ESP_LOGI(TAG, "轨迹执行模式：实际控制电机");
        
        for (uint16_t i = 0; i < total_points; i++) {
            // 检查停止请求
            if (play_stop_requested_) {
                ESP_LOGI(TAG, "收到停止请求，终止单次播放");
                return false;
            }
            
            const trajectory_point_t& point = trajectory_planner_.points[i];
            
            // 设置所有电机位置
            bool all_success = true;
            for (int j = 0; j < ARM_MOTOR_COUNT; j++) {
                if (!MotorProtocol::setPositionOnly(motor_ids_[j], point.positions[j])) {
                    ESP_LOGE(TAG, "播放点%d，电机%d位置设置失败", i, motor_ids_[j]);
                    all_success = false;
                }
            }
            
            if (all_success) {
                // 获取实际位置进行对比（使用异步更新的位置数据）
                float actual_positions[ARM_MOTOR_COUNT];
                if (getCurrentMotorPositions(actual_positions)) {
                    // 计算误差并打印
                    float errors[ARM_MOTOR_COUNT];
                    for (int j = 0; j < ARM_MOTOR_COUNT; j++) {
                        errors[j] = actual_positions[j] - point.positions[j];
                    }
                    
                    ESP_LOGW(TAG, "🎬 播放点 %d/%d: 目标=[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f] 误差=[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f]", 
                             i + 1, total_points,
                             point.positions[0], point.positions[1], point.positions[2],
                             point.positions[3], point.positions[4], point.positions[5],
                             errors[0], errors[1], errors[2],
                             errors[3], errors[4], errors[5]);
                } else {
                    ESP_LOGD(TAG, "播放轨迹点 %d/%d (位置数据无效)", i + 1, total_points);
                }
            }
            
            vTaskDelay(pdMS_TO_TICKS(play_delay_ms));
        }
        
        ESP_LOGI(TAG, "机械臂单次播放完成");
        
        // 清除播放和移动状态标志
        arm_status_.is_playing = false;
        arm_status_.is_moving = false;
        
        // 播放结束后，保持最后一个位置，避免跳变
        if (total_points > 0) {
            const trajectory_point_t& last_point = trajectory_planner_.points[total_points - 1];
            ESP_LOGI(TAG, "播放结束，保持最终位置: [%.3f, %.3f, %.3f, %.3f, %.3f, %.3f]",
                     last_point.positions[0], last_point.positions[1], last_point.positions[2],
                     last_point.positions[3], last_point.positions[4], last_point.positions[5]);
            
            // 再次设置最终位置，确保稳定
            for (int j = 0; j < ARM_MOTOR_COUNT; j++) {
                MotorProtocol::setPositionOnly(motor_ids_[j], last_point.positions[j]);
            }
        }
    } else {
        ESP_LOGI(TAG, "轨迹验证模式：仅打印轨迹点，不控制电机");
        
        // 仅打印轨迹点信息
        for (uint16_t i = 0; i < total_points; i++) {
            const trajectory_point_t& point = trajectory_planner_.points[i];
            ESP_LOGI(TAG, "播放点 %d/%d: 位置=[%.3f, %.3f, %.3f, %.3f, %.3f, %.3f]", 
                     i + 1, total_points,
                     point.positions[0], point.positions[1], point.positions[2],
                     point.positions[3], point.positions[4], point.positions[5]);
            
            vTaskDelay(pdMS_TO_TICKS(play_delay_ms));
        }
        
        ESP_LOGI(TAG, "轨迹验证完成");
        
        // 清除播放和移动状态标志
        arm_status_.is_playing = false;
        arm_status_.is_moving = false;
    }
    
    return true;
}

// 私有辅助函数实现
bool DeepArm::loadBoundaryData() {
    if (!settings_) {
        return false;
    }
    
    // 检查是否已标定
    bool is_calibrated = settings_->GetBool("bnd_cal", false);
    if (!is_calibrated) {
        ESP_LOGI(TAG, "边界数据未标定");
        return false;
    }
    
    // 加载边界数据
    for (int i = 0; i < ARM_MOTOR_COUNT; i++) {
        std::string min_key = "bnd_min_" + std::to_string(i);
        std::string max_key = "bnd_max_" + std::to_string(i);
        
        arm_boundary_.min_positions[i] = settings_->GetInt(min_key.c_str(), 0) / 1000.0f; // 转换为浮点数
        arm_boundary_.max_positions[i] = settings_->GetInt(max_key.c_str(), 0) / 1000.0f;
    }
    
    arm_boundary_.is_calibrated = true;
    arm_status_.boundary_status = BOUNDARY_CALIBRATED;
    
    ESP_LOGI(TAG, "边界数据加载成功");
    printBoundaryData();
    return true;
}

bool DeepArm::saveBoundaryData() {
    if (!settings_ || !arm_boundary_.is_calibrated) {
        return false;
    }
    
    // 保存标定状态
    settings_->SetBool("bnd_cal", true);
    
    // 保存边界数据
    for (int i = 0; i < ARM_MOTOR_COUNT; i++) {
        std::string min_key = "bnd_min_" + std::to_string(i);
        std::string max_key = "bnd_max_" + std::to_string(i);
        
        settings_->SetInt(min_key.c_str(), (int32_t)(arm_boundary_.min_positions[i] * 1000)); // 转换为整数存储
        settings_->SetInt(max_key.c_str(), (int32_t)(arm_boundary_.max_positions[i] * 1000));
    }
    
    ESP_LOGI(TAG, "边界数据保存成功");
    return true;
}

bool DeepArm::checkPositionLimits(const arm_position_t& position) {
    for (int i = 0; i < ARM_MOTOR_COUNT; i++) {
        if (position.positions[i] < arm_boundary_.min_positions[i] || 
            position.positions[i] > arm_boundary_.max_positions[i]) {
            ESP_LOGW(TAG, "电机%d位置%.3f超出边界[%.3f, %.3f]", 
                     motor_ids_[i], position.positions[i], 
                     arm_boundary_.min_positions[i], arm_boundary_.max_positions[i]);
            return false;
        }
    }
    return true;
}

void DeepArm::printBoundaryData() {
    ESP_LOGI(TAG, "=== 机械臂边界数据 ===");
    for (int i = 0; i < ARM_MOTOR_COUNT; i++) {
        ESP_LOGI(TAG, "电机%d: [%.3f, %.3f] rad", 
                 motor_ids_[i], 
                 arm_boundary_.min_positions[i], 
                 arm_boundary_.max_positions[i]);
    }
    ESP_LOGI(TAG, "======================");
}
