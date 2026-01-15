#include "deep_arm_control.h"
#include "esp_log.h"

#define TAG "DeepArmControl"

DeepArmControl::DeepArmControl(DeepArm* deep_arm, McpServer& mcp_server, CircularStrip* led_strip) 
    : deep_arm_(deep_arm) {
    
    // 初始化灯带状态管理器
    arm_led_state_ = new ArmLedState(led_strip);
    
    // 设置初始状态为未初始化（红色慢闪）
    arm_led_state_->SetArmState(ArmLedState::ArmState::UNINITIALIZED);
    
    // ========== 机械臂基本控制工具 ==========
    
    // 设置机械臂零位
    mcp_server.AddTool("self.arm.set_zero_position", "设置机械臂零位", PropertyList(), [this](const PropertyList&) -> ReturnValue {
        if (!deep_arm_) {
            ESP_LOGW(TAG, "机械臂控制器未初始化");
            return std::string("机械臂控制器未初始化");
        }
        
        if (deep_arm_->setZeroPosition()) {
            ESP_LOGI(TAG, "设置机械臂零位成功");
            return std::string("设置机械臂零位成功");
        } else {
            ESP_LOGE(TAG, "设置机械臂零位失败");
            return std::string("设置机械臂零位失败");
        }
    });

    // 启动机械臂
    mcp_server.AddTool("self.arm.enable", "启动机械臂", PropertyList(), [this](const PropertyList&) -> ReturnValue {
        // if (!deep_arm_) {
        //     ESP_LOGW(TAG, "机械臂控制器未初始化");
        //     return std::string("机械臂控制器未初始化");
        // }
        
        if (deep_arm_->enableArm()) {
            ESP_LOGI(TAG, "启动机械臂成功");
            UpdateArmStatus();  // 更新灯带状态
            return std::string("启动机械臂成功");
        } else {
            ESP_LOGE(TAG, "启动机械臂失败");
            return std::string("启动机械臂失败，请先初始化机械臂");
        }
    });

    // 关闭机械臂
    mcp_server.AddTool("self.arm.disable", "关闭机械臂", PropertyList(), [this](const PropertyList&) -> ReturnValue {
        if (!deep_arm_) {
            ESP_LOGW(TAG, "机械臂控制器未初始化");
            return std::string("机械臂控制器未初始化");
        }
        
        if (deep_arm_->disableArm()) {
            ESP_LOGI(TAG, "关闭机械臂成功");
            UpdateArmStatus();  // 更新灯带状态
            return std::string("关闭机械臂成功");
        } else {
            ESP_LOGE(TAG, "关闭机械臂失败");
            return std::string("关闭机械臂失败");
        }
    });

    // 初始化机械臂
    mcp_server.AddTool("self.arm.initialize", "初始化机械臂", PropertyList(std::vector<Property>{
        Property("max_speed", kPropertyTypeInteger, 300, 10, 300)
    }), [this](const PropertyList& properties) -> ReturnValue {
        // if (!deep_arm_) {
        //     ESP_LOGW(TAG, "机械臂控制器未初始化");
        //     return std::string("机械臂控制器未初始化");
        // }
        
        int max_speed_int = properties["max_speed"].value<int>();
        float max_speed = max_speed_int / 10.0f; // 转换为浮点数（保留1位小数）
        
        // 为所有6个电机设置相同的最大速度
        float max_speeds[6] = {max_speed, max_speed, max_speed, max_speed, max_speed, max_speed};
        
        if (deep_arm_->initializeArm(max_speeds)) {
            ESP_LOGI(TAG, "机械臂初始化成功，最大速度: %.1f rad/s", max_speed);
            UpdateArmStatus();  // 更新灯带状态
            return std::string("机械臂初始化成功，最大速度: " + std::to_string(max_speed) + " rad/s");
        } else {
            ESP_LOGE(TAG, "机械臂初始化失败");
            return std::string("机械臂初始化失败，请确保机械臂处于关闭状态");
        }
    });

    // ========== 机械臂位置控制工具 ==========
    
    // 设置机械臂位置
    mcp_server.AddTool("self.arm.set_position", "设置机械臂位置", PropertyList(std::vector<Property>{
        Property("pos1", kPropertyTypeInteger, 0, -314, 314),
        Property("pos2", kPropertyTypeInteger, 0, -314, 314),
        Property("pos3", kPropertyTypeInteger, 0, -314, 314),
        Property("pos4", kPropertyTypeInteger, 0, -314, 314),
        Property("pos5", kPropertyTypeInteger, 0, -314, 314),
        Property("pos6", kPropertyTypeInteger, 0, -314, 314)
    }), [this](const PropertyList& properties) -> ReturnValue {
        if (!deep_arm_) {
            ESP_LOGW(TAG, "机械臂控制器未初始化");
            return std::string("机械臂控制器未初始化");
        }
        
        // 获取6个关节的位置参数
        float positions[6];
        positions[0] = properties["pos1"].value<int>() / 100.0f; // 转换为浮点数（保留2位小数）
        positions[1] = properties["pos2"].value<int>() / 100.0f;
        positions[2] = properties["pos3"].value<int>() / 100.0f;
        positions[3] = properties["pos4"].value<int>() / 100.0f;
        positions[4] = properties["pos5"].value<int>() / 100.0f;
        positions[5] = properties["pos6"].value<int>() / 100.0f;
        
        if (deep_arm_->setArmPosition(positions)) {
            ESP_LOGI(TAG, "设置机械臂位置成功: [%.2f, %.2f, %.2f, %.2f, %.2f, %.2f]", 
                     positions[0], positions[1], positions[2], positions[3], positions[4], positions[5]);
            return std::string("设置机械臂位置成功");
        } else {
            ESP_LOGE(TAG, "设置机械臂位置失败");
            return std::string("设置机械臂位置失败");
        }
    });

    // ========== 机械臂录制功能工具 ==========
    
    // 开始录制
    mcp_server.AddTool("self.arm.start_recording", "开始机械臂录制", PropertyList(), [this](const PropertyList&) -> ReturnValue {
        if (!deep_arm_) {
            ESP_LOGW(TAG, "机械臂控制器未初始化");
            return std::string("机械臂控制器未初始化");
        }
        
        if (deep_arm_->startRecording()) {
            ESP_LOGI(TAG, "开始机械臂录制成功");
            UpdateArmStatus();  // 更新灯带状态
            return std::string("开始机械臂录制成功，可以开始拖动机械臂");
        } else {
            ESP_LOGE(TAG, "开始机械臂录制失败");
            return std::string("开始机械臂录制失败，请先初始化机械臂");
        }
    });

    // 停止录制
    mcp_server.AddTool("self.arm.stop_recording", "停止机械臂录制", PropertyList(), [this](const PropertyList&) -> ReturnValue {
        if (!deep_arm_) {
            ESP_LOGW(TAG, "机械臂控制器未初始化");
            return std::string("机械臂控制器未初始化");
        }
        
        if (deep_arm_->stopRecording()) {
            uint16_t point_count = deep_arm_->getRecordingPointCount();
            ESP_LOGI(TAG, "停止机械臂录制成功，共记录 %d 个录制点", point_count);
            UpdateArmStatus();  // 更新灯带状态
            return std::string("停止机械臂录制成功，共记录 " + std::to_string(point_count) + " 个录制点");
        } else {
            ESP_LOGE(TAG, "停止机械臂录制失败");
            return std::string("停止机械臂录制失败");
        }
    });

    // 播放录制
    mcp_server.AddTool("self.arm.play_recording", "播放机械臂录制", PropertyList(std::vector<Property>{
        Property("loop_count", kPropertyTypeInteger, 1)  // 默认值为1次
    }), [this](const PropertyList& props) -> ReturnValue {
        if (!deep_arm_) {
            ESP_LOGW(TAG, "机械臂控制器未初始化");
            return std::string("机械臂控制器未初始化");
        }
        
        // 获取循环次数参数
        int32_t loop_count = props["loop_count"].value<int>();
        
        // 参数验证
        if (loop_count == 0) {
            return std::string("错误：循环次数不能为0，请使用1播放一次，-1无限循环，或2-N播放指定次数");
        }
        
        // 转换参数：-1表示无限循环，其他值直接使用
        uint32_t actual_loop_count = (loop_count == -1) ? 0 : static_cast<uint32_t>(loop_count);
        
        if (deep_arm_->playRecording(actual_loop_count)) {
            uint16_t point_count = deep_arm_->getRecordingPointCount();
            std::string loop_desc = (loop_count == -1) ? "无限循环" : std::to_string(loop_count) + "次";
            ESP_LOGI(TAG, "播放机械臂录制成功，总点数: %d，循环次数: %s", point_count, loop_desc.c_str());
            UpdateArmStatus();  // 更新灯带状态
            return std::string("播放机械臂录制成功，总点数: " + std::to_string(point_count) + "，循环次数: " + loop_desc);
        } else {
            ESP_LOGE(TAG, "播放机械臂录制失败");
            return std::string("播放机械臂录制失败，请先初始化机械臂并完成录制");
        }
    });

    // 停止播放录制
    mcp_server.AddTool("self.arm.stop_playback", "停止播放机械臂录制", PropertyList(), [this](const PropertyList&) -> ReturnValue {
        if (!deep_arm_) {
            ESP_LOGW(TAG, "机械臂控制器未初始化");
            return std::string("机械臂控制器未初始化");
        }
        
        if (deep_arm_->stopPlayback()) {
            ESP_LOGI(TAG, "停止播放机械臂录制成功");
            UpdateArmStatus();  // 更新灯带状态
            return std::string("停止播放机械臂录制成功");
        } else {
            ESP_LOGE(TAG, "停止播放机械臂录制失败");
            return std::string("停止播放机械臂录制失败");
        }
    });

    // 获取录制状态
    mcp_server.AddTool("self.arm.get_recording_status", "获取机械臂录制状态", PropertyList(), [this](const PropertyList&) -> ReturnValue {
        if (!deep_arm_) {
            ESP_LOGW(TAG, "机械臂控制器未初始化");
            return std::string("机械臂控制器未初始化");
        }
        
        bool is_recording = deep_arm_->isRecording();
        bool data_ready = deep_arm_->isRecordingDataReady();
        uint16_t point_count = deep_arm_->getRecordingPointCount();
        
        std::string result = "机械臂录制状态:\n";
        result += "  录制模式: " + std::string(is_recording ? "进行中" : "未启动") + "\n";
        result += "  数据就绪: " + std::string(data_ready ? "是" : "否") + "\n";
        result += "  录制点数: " + std::to_string(point_count);
        
        ESP_LOGI(TAG, "机械臂录制状态 - 模式: %s, 数据就绪: %s, 点数: %d", 
                 is_recording ? "进行中" : "未启动", 
                 data_ready ? "是" : "否", 
                 point_count);
        
        return result;
    });

    // 获取播放状态
    mcp_server.AddTool("self.arm.get_playback_status", "获取机械臂播放状态", PropertyList(), [this](const PropertyList&) -> ReturnValue {
        if (!deep_arm_) {
            ESP_LOGW(TAG, "机械臂控制器未初始化");
            return std::string("机械臂控制器未初始化");
        }
        
        // 检查是否有播放任务在运行
        bool is_playing = deep_arm_->isPlaying();
        
        std::string result = "机械臂播放状态:\n";
        result += "  播放状态: " + std::string(is_playing ? "播放中" : "未播放") + "\n";
        
        if (is_playing) {
            result += "  提示: 使用 self.arm.stop_playback 可以停止播放";
        }
        
        ESP_LOGI(TAG, "机械臂播放状态 - 状态: %s", is_playing ? "播放中" : "未播放");
        
        return result;
    });

    // ========== 机械臂状态查询工具 ==========
    
    // 启动状态查询任务
    mcp_server.AddTool("self.arm.start_status_task", "启动机械臂状态查询任务", PropertyList(), [this](const PropertyList&) -> ReturnValue {
        if (!deep_arm_) {
            ESP_LOGW(TAG, "机械臂控制器未初始化");
            return std::string("机械臂控制器未初始化");
        }
        
        if (deep_arm_->startStatusQueryTask()) {
            ESP_LOGI(TAG, "启动机械臂状态查询任务成功");
            return std::string("启动机械臂状态查询任务成功");
        } else {
            ESP_LOGE(TAG, "启动机械臂状态查询任务失败");
            return std::string("启动机械臂状态查询任务失败");
        }
    });

    // 停止状态查询任务
    mcp_server.AddTool("self.arm.stop_status_task", "停止机械臂状态查询任务", PropertyList(), [this](const PropertyList&) -> ReturnValue {
        if (!deep_arm_) {
            ESP_LOGW(TAG, "机械臂控制器未初始化");
            return std::string("机械臂控制器未初始化");
        }
        
        if (deep_arm_->stopStatusQueryTask()) {
            ESP_LOGI(TAG, "停止机械臂状态查询任务成功");
            return std::string("停止机械臂状态查询任务成功");
        } else {
            ESP_LOGW(TAG, "停止机械臂状态查询任务失败");
            return std::string("停止机械臂状态查询任务失败");
        }
    });

    // 获取机械臂状态
    mcp_server.AddTool("self.arm.get_status", "获取机械臂状态", PropertyList(), [this](const PropertyList&) -> ReturnValue {
        if (!deep_arm_) {
            ESP_LOGW(TAG, "机械臂控制器未初始化");
            return std::string("机械臂控制器未初始化");
        }
        
        arm_status_t status;
        if (deep_arm_->getArmStatus(&status)) {
            std::string result = "机械臂状态:\n";
            result += "  已初始化: " + std::string(status.is_initialized ? "是" : "否") + "\n";
            result += "  已启动: " + std::string(status.is_enabled ? "是" : "否") + "\n";
            result += "  录制中: " + std::string(status.is_recording ? "是" : "否") + "\n";
            result += "  录制数据就绪: " + std::string(status.recording_data_ready ? "是" : "否") + "\n";
            result += "  录制点数: " + std::to_string(status.recording_point_count) + "\n";
            result += "  边界标定状态: " + std::to_string(status.boundary_status) + "\n";
            
            // 添加操作建议
            result += "\n操作建议:\n";
            if (!status.is_initialized) {
                result += "  → 请先执行: self.arm.initialize\n";
            } else if (!status.is_enabled && !status.is_recording) {
                result += "  → 可以执行: self.arm.enable 或 self.arm.start_recording\n";
            } else if (status.is_enabled && status.recording_data_ready) {
                result += "  → 可以执行: self.arm.play_recording\n";
            } else if (status.is_recording) {
                result += "  → 可以执行: self.arm.stop_recording\n";
            }
            
            ESP_LOGI(TAG, "机械臂状态 - 初始化: %s, 启动: %s, 录制: %s, 边界: %d", 
                     status.is_initialized ? "是" : "否",
                     status.is_enabled ? "是" : "否",
                     status.is_recording ? "是" : "否",
                     status.boundary_status);
            
            return result;
        } else {
            ESP_LOGE(TAG, "获取机械臂状态失败");
            return std::string("获取机械臂状态失败");
        }
    });

    // 打印机械臂状态
    mcp_server.AddTool("self.arm.print_status", "打印机械臂状态", PropertyList(), [this](const PropertyList&) -> ReturnValue {
        if (!deep_arm_) {
            ESP_LOGW(TAG, "机械臂控制器未初始化");
            return std::string("机械臂控制器未初始化");
        }
        
        deep_arm_->printArmStatus();
        return std::string("已打印机械臂状态到日志");
    });

    // ========== 机械臂边界标定工具 ==========
    
    // 开始边界标定
    mcp_server.AddTool("self.arm.start_boundary_calibration", "开始机械臂边界标定", PropertyList(), [this](const PropertyList&) -> ReturnValue {
        if (!deep_arm_) {
            ESP_LOGW(TAG, "机械臂控制器未初始化");
            return std::string("机械臂控制器未初始化");
        }
        
        if (deep_arm_->startBoundaryCalibration()) {
            ESP_LOGI(TAG, "开始机械臂边界标定成功");
            UpdateArmStatus();  // 更新灯带状态
            return std::string("开始机械臂边界标定成功，请依次转动每个关节");
        } else {
            ESP_LOGE(TAG, "开始机械臂边界标定失败");
            return std::string("开始机械臂边界标定失败");
        }
    });

    // 停止边界标定
    mcp_server.AddTool("self.arm.stop_boundary_calibration", "停止机械臂边界标定", PropertyList(), [this](const PropertyList&) -> ReturnValue {
        if (!deep_arm_) {
            ESP_LOGW(TAG, "机械臂控制器未初始化");
            return std::string("机械臂控制器未初始化");
        }
        
        if (deep_arm_->stopBoundaryCalibration()) {
            ESP_LOGI(TAG, "停止机械臂边界标定成功");
            UpdateArmStatus();  // 更新灯带状态
            return std::string("停止机械臂边界标定成功");
        } else {
            ESP_LOGE(TAG, "停止机械臂边界标定失败");
            return std::string("停止机械臂边界标定失败");
        }
    });

    // 获取边界数据
    mcp_server.AddTool("self.arm.get_boundary", "获取机械臂边界数据", PropertyList(), [this](const PropertyList&) -> ReturnValue {
        if (!deep_arm_) {
            ESP_LOGW(TAG, "机械臂控制器未初始化");
            return std::string("机械臂控制器未初始化");
        }
        
        arm_boundary_t boundary;
        if (deep_arm_->getArmBoundary(&boundary)) {
            std::string result = "机械臂边界数据:\n";
            result += "  已标定: " + std::string(boundary.is_calibrated ? "是" : "否") + "\n";
            
            if (boundary.is_calibrated) {
                result += "  各关节边界范围:\n";
                for (int i = 0; i < 6; i++) {
                    result += "    关节" + std::to_string(i + 1) + ": [" + 
                             std::to_string(boundary.min_positions[i]) + ", " + 
                             std::to_string(boundary.max_positions[i]) + "] rad\n";
                }
            } else {
                result += "  边界未标定，请先进行边界标定";
            }
            
            ESP_LOGI(TAG, "机械臂边界数据 - 已标定: %s", boundary.is_calibrated ? "是" : "否");
            return result;
        } else {
            ESP_LOGE(TAG, "获取机械臂边界数据失败");
            return std::string("获取机械臂边界数据失败");
        }
    });

    // 检查边界是否已标定
    mcp_server.AddTool("self.arm.is_boundary_calibrated", "检查机械臂边界是否已标定", PropertyList(), [this](const PropertyList&) -> ReturnValue {
        if (!deep_arm_) {
            ESP_LOGW(TAG, "机械臂控制器未初始化");
            return std::string("机械臂控制器未初始化");
        }
        
        bool is_calibrated = deep_arm_->isBoundaryCalibrated();
        std::string result = "机械臂边界标定状态: " + std::string(is_calibrated ? "已标定" : "未标定");
        
        ESP_LOGI(TAG, "机械臂边界标定状态: %s", is_calibrated ? "已标定" : "未标定");
        return result;
    });

    ESP_LOGI(TAG, "机械臂控制MCP工具注册完成，包含基本控制、录制、边界标定等功能");
}

DeepArmControl::~DeepArmControl() {
    if (arm_led_state_) {
        delete arm_led_state_;
        arm_led_state_ = nullptr;
    }
}

void DeepArmControl::UpdateArmStatus() {
    if (!deep_arm_ || !arm_led_state_) {
        return;
    }
    
    arm_status_t status;
    if (deep_arm_->getArmStatus(&status)) {
        arm_led_state_->UpdateFromArmStatus(status);
    }
}
