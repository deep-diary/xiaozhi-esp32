#include "dog_control.h"
#include "motor/deep_motor.h"
#include "mcp_server.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "DogControl"

DogControl::DogControl() {
    legs_[0].setLegType(LegType::FL);
    legs_[1].setLegType(LegType::FR);
    legs_[2].setLegType(LegType::RL);
    legs_[3].setLegType(LegType::RR);
}

void DogControl::setDeepMotor(DeepMotor* motor) {
    deep_motor_ = motor;
    for (int i = 0; i < 4; i++) {
        legs_[i].setDeepMotor(motor);
    }
}

void DogControl::getLegs(LegControl* out_legs[4]) {
    for (int i = 0; i < 4; i++) {
        out_legs[i] = &legs_[i];
    }
}

bool DogControl::init() {
    if (initialized_) {
        ESP_LOGI(TAG, "dog init skipped: already initialized");
        return true;
    }
    if (!deep_motor_) {
        ESP_LOGE(TAG, "DeepMotor not set");
        return false;
    }
    for (int i = 0; i < 4; i++) {
        if (!legs_[i].init()) {
            ESP_LOGE(TAG, "leg %d init failed", i);
            return false;
        }
    }
    ESP_LOGI(TAG, "dog init ok, 4 legs");
    initialized_ = true;
    return true;
}

bool DogControl::stand(float max_speed_rad_s) {
    if (!deep_motor_) return false;
    for (int i = 0; i < 4; i++) {
        if (!legs_[i].goToStance(max_speed_rad_s)) {
            ESP_LOGE(TAG, "leg %d goToStance failed", i);
            return false;
        }
    }
    return true;
}

bool DogControl::lieDown(float max_speed_rad_s) {
    if (!deep_motor_) return false;
    for (int i = 0; i < 4; i++) {
        if (!legs_[i].goToZero(max_speed_rad_s)) {
            ESP_LOGE(TAG, "leg %d goToZero failed", i);
            return false;
        }
    }
    return true;
}

bool DogControl::goForward(float max_speed_rad_s) {
    if (!deep_motor_) return false;
    for (int i = 0; i < 4; i++) {
        if (!legs_[i].stepForward(max_speed_rad_s)) {
            ESP_LOGE(TAG, "leg %d stepForward failed", i);
            return false;
        }
    }
    return true;
}

bool DogControl::goBack(float max_speed_rad_s) {
    if (!deep_motor_) return false;
    for (int i = 0; i < 4; i++) {
        if (!legs_[i].stepBackward(max_speed_rad_s)) {
            ESP_LOGE(TAG, "leg %d stepBackward failed", i);
            return false;
        }
    }
    return true;
}

bool DogControl::disable() {
    for (int i = 0; i < 4; i++) {
        if (!legs_[i].disable()) {
            ESP_LOGE(TAG, "leg %d disable failed", i);
            return false;
        }
    }
    return true;
}

bool DogControl::dance(float max_speed_rad_s) {
    if (!stand(max_speed_rad_s)) return false;
    vTaskDelay(pdMS_TO_TICKS(800));
    for (int i = 0; i < 4; i++) {
        if (!goForward(max_speed_rad_s)) return false;
        vTaskDelay(pdMS_TO_TICKS(400));
    }
    for (int i = 0; i < 4; i++) {
        if (!goBack(max_speed_rad_s)) return false;
        vTaskDelay(pdMS_TO_TICKS(400));
    }
    if (!stand(max_speed_rad_s)) return false;
    ESP_LOGI(TAG, "dance done");
    return true;
}

// --- MCP 工具注册 ---

void RegisterDogMcpTools(McpServer& mcp_server, DogControl* dog) {
    if (!dog) return;

    mcp_server.AddTool("self.dog.init", "机器狗整机初始化（使能 12 个电机）。用户说：初始化、整机初始化、机器狗初始化 时调用", PropertyList(), [dog](const PropertyList&) -> ReturnValue {
        if (dog->init()) return std::string("机器狗初始化成功");
        return std::string("机器狗初始化失败");
    });

    mcp_server.AddTool("self.dog.stand", "机器狗整机站立，四条腿回站立位。用户说：站起来、站立、站起、机器狗站立 时调用", PropertyList(), [dog](const PropertyList&) -> ReturnValue {
        if (dog->stand()) return std::string("机器狗已站立");
        return std::string("机器狗站立失败");
    });

    mcp_server.AddTool("self.dog.lie_down", "机器狗整机卧倒，四条腿回零位。用户说：卧倒、趴下、回零位、机器狗卧倒 时调用", PropertyList(), [dog](const PropertyList&) -> ReturnValue {
        if (dog->lieDown()) return std::string("机器狗已卧倒");
        return std::string("机器狗卧倒失败");
    });

    mcp_server.AddTool("self.chassis.go_forward", "机器狗向前走一步。用户说：前进、往前走、向前 时调用", PropertyList(), [dog](const PropertyList&) -> ReturnValue {
        if (dog->goForward()) return std::string("已向前一步");
        return std::string("向前一步失败");
    });

    mcp_server.AddTool("self.chassis.go_back", "机器狗向后走一步。用户说：后退、往后走、向后 时调用", PropertyList(), [dog](const PropertyList&) -> ReturnValue {
        if (dog->goBack()) return std::string("已向后一步");
        return std::string("向后一步失败");
    });

    mcp_server.AddTool("self.chassis.dance", "机器狗跳舞（站立→前进四步→后退四步→站立）。用户说：跳舞、来段舞 时调用", PropertyList(), [dog](const PropertyList&) -> ReturnValue {
        if (dog->dance()) return std::string("跳舞完成");
        return std::string("跳舞失败");
    });

    ESP_LOGI(TAG, "Dog MCP tools registered: self.dog.init, stand, lie_down; self.chassis.go_forward, go_back, dance");
}
