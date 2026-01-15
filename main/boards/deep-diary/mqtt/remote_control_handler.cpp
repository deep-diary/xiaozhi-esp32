#include "remote_control_handler.h"
#include "../config.h"
#include "esp_log.h"
#include "esp_system.h"
#include <cJSON.h>

#if ENABLE_TCP_CLIENT_MODE
#include "../streaming/tcp_client.h"
#endif

#define TAG_REMOTE_CONTROL "RemoteControl"

RemoteControlHandler::RemoteControlHandler() 
    : deep_motor_(nullptr), deep_arm_(nullptr), gimbal_(nullptr), 
      led_strip_(nullptr), led_control_(nullptr), camera_(nullptr), mqtt_client_(nullptr) {
    ESP_LOGI(TAG_REMOTE_CONTROL, "RemoteControlHandler initialized");
}

RemoteControlHandler::~RemoteControlHandler() {
    ESP_LOGI(TAG_REMOTE_CONTROL, "RemoteControlHandler destroyed");
}

void RemoteControlHandler::SetDeepMotor(DeepMotor* motor) {
    deep_motor_ = motor;
}

void RemoteControlHandler::SetDeepArm(DeepArm* arm) {
    deep_arm_ = arm;
}

void RemoteControlHandler::SetGimbal(Gimbal_t* gimbal) {
    gimbal_ = gimbal;
}

void RemoteControlHandler::SetLedStrip(CircularStrip* led_strip) {
    led_strip_ = led_strip;
}

void RemoteControlHandler::SetLedControl(LedStripControl* led_control) {
    led_control_ = led_control;
    // 如果led_control_存在，也更新led_strip_引用（用于向后兼容）
    if (led_control_ && !led_strip_) {
        // 注意：LedStripControl没有提供获取led_strip_的方法，所以这里保持原样
        // 实际上，led_strip_应该通过SetLedStrip单独设置
    }
}

void RemoteControlHandler::SetCamera(Esp32Camera* camera) {
    camera_ = camera;
}

void RemoteControlHandler::SetMqttClient(UserMqttClient* mqtt_client) {
    mqtt_client_ = mqtt_client;
}

// ==================== Thumbler 命令处理 ====================

void RemoteControlHandler::HandleThumblerCommand(const ThumblerControlCommand& command) {
    ESP_LOGI(TAG_REMOTE_CONTROL, "🎮 HANDLING Thumbler Command");
    
    // 发送收到指令的事件反馈
    cJSON* event_data = cJSON_CreateObject();
    bool has_any_command = false;
    
    // 处理 LED 控制
    if (command.has_tar_led_mode || command.has_tar_led_brightness || 
        command.has_tar_led_color_red || command.has_tar_led_color_green || 
        command.has_tar_led_color_blue) {
        HandleThumblerLedControl(command);
        has_any_command = true;
        cJSON_AddStringToObject(event_data, "led_control", "executed");
    }
    
    // 处理摄像头控制
    if (command.has_tar_cam_switch) {
        HandleThumblerCameraControl(command);
        has_any_command = true;
        cJSON_AddStringToObject(event_data, "camera_control", "executed");
    }
    
    // 处理不倒翁控制（俯仰角和翻滚角）
    if (command.has_tar_tumbler_mode || command.has_tar_pitch || command.has_tar_roll) {
        HandleThumblerTumblerControl(command);
        has_any_command = true;
        cJSON_AddStringToObject(event_data, "tumbler_control", "executed");
    }
    
    // 发送事件反馈
    if (has_any_command) {
        SendEvent("command_received", "Thumbler command received and executed", event_data);
    }
    
    cJSON_Delete(event_data);
}

void RemoteControlHandler::HandleThumblerLedControl(const ThumblerControlCommand& command) {
    // 优先使用led_control_，确保状态同步；如果没有则使用led_strip_（向后兼容）
    CircularStrip* strip = led_control_ ? nullptr : led_strip_;
    if (!led_control_ && !led_strip_) {
        ESP_LOGW(TAG_REMOTE_CONTROL, "LED strip not available");
        return;
    }
    
    ESP_LOGI(TAG_REMOTE_CONTROL, "🎨 Handling Thumbler LED Control");
    
    // 设置亮度
    if (command.has_tar_led_brightness || command.has_tar_led_low_brightness) {
        uint8_t default_brightness = command.has_tar_led_brightness ? 
            (uint8_t)command.tar_led_brightness : 128;
        uint8_t low_brightness = command.has_tar_led_low_brightness ? 
            (uint8_t)command.tar_led_low_brightness : 16;
        if (led_control_ && led_strip_) {
            // 直接调用led_strip_设置亮度，然后更新led_control_的状态
            led_strip_->SetBrightness(default_brightness, low_brightness);
            led_control_->UpdateBrightness(default_brightness, low_brightness);
        } else if (strip) {
            strip->SetBrightness(default_brightness, low_brightness);
        }
    }
    
    // 根据 LED 模式执行相应操作
    if (command.has_tar_led_mode) {
        StripColor main_color = {
            (uint8_t)(command.has_tar_led_color_red ? command.tar_led_color_red : 0),
            (uint8_t)(command.has_tar_led_color_green ? command.tar_led_color_green : 0),
            (uint8_t)(command.has_tar_led_color_blue ? command.tar_led_color_blue : 0)
        };
        
        StripColor low_color = {
            (uint8_t)(command.has_tar_led_color_low_red ? command.tar_led_color_low_red : 0),
            (uint8_t)(command.has_tar_led_color_low_green ? command.tar_led_color_low_green : 0),
            (uint8_t)(command.has_tar_led_color_low_blue ? command.tar_led_color_low_blue : 0)
        };
        
        int interval_ms = command.has_tar_led_interval_ms ? command.tar_led_interval_ms : 500;
        int scroll_length = command.has_tar_led_scroll_length ? command.tar_led_scroll_length : 3;
        
        // 优先使用led_control_的方法（通过MCP工具），确保状态同步
        // 但由于LedStripControl的方法都是通过MCP工具注册的，我们需要直接调用led_strip_
        // 并在led_control_中同步状态
        CircularStrip* target_strip = led_control_ ? led_strip_ : strip;
        if (!target_strip) {
            ESP_LOGW(TAG_REMOTE_CONTROL, "LED strip not available");
            return;
        }
        
        switch (command.tar_led_mode) {
            case 0: // 关闭
                target_strip->SetAllColor({0, 0, 0});
                if (led_control_) {
                    led_control_->UpdateState(0, {0, 0, 0});
                }
                ESP_LOGI(TAG_REMOTE_CONTROL, "LED mode: OFF");
                break;
                
            case 1: // 静态颜色
                target_strip->SetAllColor(main_color);
                if (led_control_) {
                    led_control_->UpdateState(1, main_color);
                }
                ESP_LOGI(TAG_REMOTE_CONTROL, "LED mode: STATIC COLOR (R:%d G:%d B:%d)", 
                         main_color.red, main_color.green, main_color.blue);
                break;
                
            case 2: // 闪烁
                target_strip->Blink(main_color, interval_ms);
                if (led_control_) {
                    led_control_->UpdateState(2, main_color, interval_ms);
                }
                ESP_LOGI(TAG_REMOTE_CONTROL, "LED mode: BLINK (interval: %dms)", interval_ms);
                break;
                
            case 3: // 呼吸灯
                target_strip->Breathe(low_color, main_color, interval_ms);
                if (led_control_) {
                    led_control_->UpdateState(3, main_color, low_color, interval_ms);
                }
                ESP_LOGI(TAG_REMOTE_CONTROL, "LED mode: BREATHE (interval: %dms)", interval_ms);
                break;
                
            case 4: // 流水灯/滚动
                target_strip->Scroll(low_color, main_color, scroll_length, interval_ms);
                if (led_control_) {
                    led_control_->UpdateState(4, main_color, low_color, interval_ms, scroll_length);
                }
                ESP_LOGI(TAG_REMOTE_CONTROL, "LED mode: SCROLL (length: %d, interval: %dms)", 
                         scroll_length, interval_ms);
                break;
                
            case 5: // 系统状态
                target_strip->OnStateChanged();
                if (led_control_) {
                    led_control_->UpdateState(5, {0, 0, 0});
                }
                ESP_LOGI(TAG_REMOTE_CONTROL, "LED mode: SYSTEM STATE");
                break;
                
            default:
                ESP_LOGW(TAG_REMOTE_CONTROL, "Unknown LED mode: %d", command.tar_led_mode);
                break;
        }
    }
}

void RemoteControlHandler::HandleThumblerCameraControl(const ThumblerControlCommand& command) {
    if (!camera_) {
        ESP_LOGW(TAG_REMOTE_CONTROL, "Camera not available");
        return;
    }
    
    ESP_LOGI(TAG_REMOTE_CONTROL, "📷 Handling Thumbler Camera Control: %s", 
             command.tar_cam_switch ? "ON" : "OFF");
    
#if ENABLE_TCP_CLIENT_MODE
    if (command.tar_cam_switch) {
        // 启动TCP客户端推流
        tcp_client_state_t current_state = tcp_client_get_state();
        if (current_state == TCP_CLIENT_DISCONNECTED || current_state == TCP_CLIENT_ERROR) {
            ESP_LOGI(TAG_REMOTE_CONTROL, "Starting TCP client for camera streaming...");
            esp_err_t ret = tcp_client_start();
            if (ret == ESP_OK) {
                ESP_LOGI(TAG_REMOTE_CONTROL, "TCP client started successfully");
            } else {
                ESP_LOGE(TAG_REMOTE_CONTROL, "Failed to start TCP client");
            }
        } else {
            ESP_LOGI(TAG_REMOTE_CONTROL, "TCP client already running (state: %d)", current_state);
        }
    } else {
        // 停止TCP客户端推流
        // 无论当前状态如何，都尝试停止TCP客户端
        // tcp_client_stop() 内部会检查是否在运行，如果未运行会直接返回
        ESP_LOGI(TAG_REMOTE_CONTROL, "Stopping TCP client...");
        esp_err_t ret = tcp_client_stop();
        if (ret == ESP_OK) {
            ESP_LOGI(TAG_REMOTE_CONTROL, "TCP client stopped successfully");
        } else {
            ESP_LOGE(TAG_REMOTE_CONTROL, "Failed to stop TCP client");
        }
    }
#else
    ESP_LOGW(TAG_REMOTE_CONTROL, "TCP client mode not enabled");
#endif
    
    ESP_LOGI(TAG_REMOTE_CONTROL, "Camera switch: %s", command.tar_cam_switch ? "ON" : "OFF");
}

void RemoteControlHandler::HandleThumblerTumblerControl(const ThumblerControlCommand& command) {
    ESP_LOGI(TAG_REMOTE_CONTROL, "🎯 Handling Thumbler Control");
    
    if (command.has_tar_tumbler_mode) {
        ESP_LOGI(TAG_REMOTE_CONTROL, "Tumbler mode: %d", command.tar_tumbler_mode);
        // TODO: 实现不倒翁模式控制
        // 这里需要根据实际的不倒翁控制 API 来实现
    }
    
    // 处理俯仰角和翻滚角控制（通过云台）
    if (gimbal_ && Gimbal_isInitialized(gimbal_)) {
        int current_pan = Gimbal_getPanAngle(gimbal_);
        int current_tilt = Gimbal_getTiltAngle(gimbal_);
        bool angle_changed = false;
        
        // 俯仰角(pitch)对应垂直舵机(tilt)，范围0-180度
        if (command.has_tar_pitch) {
            // tar_pitch 范围是 -90 ~ 90，转换为 tilt_angle 0~180 度
            float pitch = command.tar_pitch;
            if (pitch < -90.0f) pitch = -90.0f;
            if (pitch > 90.0f) pitch = 90.0f;
            // 线性映射: -90 -> 0, 0 -> 90, 90 -> 180
            int tilt_angle = (int)((pitch + 90.0f) * 1.0f); // (pitch + 90) = 0~180
            if (tilt_angle < 0) tilt_angle = 0;
            if (tilt_angle > 180) tilt_angle = 180;

            current_tilt = tilt_angle;
            angle_changed = true;
            ESP_LOGI(TAG_REMOTE_CONTROL, "Setting tilt (pitch) angle: %d° (tar_pitch: %.2f°)", tilt_angle, pitch);
        }
        
        // 翻滚角(roll)对应水平舵机(pan)，范围0-270度
        if (command.has_tar_roll) {
            // 将浮点角度转换为整数，并限制在0-270度范围内
            int pan_angle = (int)command.tar_roll;
            if (pan_angle < 0) pan_angle = 0;
            if (pan_angle > 270) pan_angle = 270;
            
            current_pan = pan_angle;
            angle_changed = true;
            ESP_LOGI(TAG_REMOTE_CONTROL, "Setting pan (roll) angle: %d°", pan_angle);
        }
        
        // 如果角度有变化，更新云台
        if (angle_changed) {
            Gimbal_setAngles(gimbal_, current_pan, current_tilt);
            ESP_LOGI(TAG_REMOTE_CONTROL, "Gimbal angles updated - Pan: %d°, Tilt: %d°", current_pan, current_tilt);
        }
    } else {
        if (command.has_tar_pitch) {
            ESP_LOGW(TAG_REMOTE_CONTROL, "Gimbal not available, cannot set pitch: %.2f°", command.tar_pitch);
        }
        if (command.has_tar_roll) {
            ESP_LOGW(TAG_REMOTE_CONTROL, "Gimbal not available, cannot set roll: %.2f°", command.tar_roll);
        }
    }
}

void RemoteControlHandler::SendEvent(const std::string& event_type, const std::string& message, const cJSON* data) {
    if (mqtt_client_) {
        mqtt_client_->SendEvent(event_type, message, data);
    } else {
        ESP_LOGW(TAG_REMOTE_CONTROL, "MQTT client not set, cannot send event: %s", event_type.c_str());
    }
}

