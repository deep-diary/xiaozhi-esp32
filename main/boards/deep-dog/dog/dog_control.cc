#include "dog_control.h"
#include "motor/deep_motor.h"
#include "mcp_server.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cmath>
#include <string>
#include <vector>

#define TAG "DogControl"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

static inline float dog_rad_to_deg(float rad) {
    return rad * 180.0f / (float)M_PI;
}

DogControl::DogControl() {
    legs_[0].setLegType(LegType::FL);
    legs_[1].setLegType(LegType::FR);
    legs_[2].setLegType(LegType::RL);
    legs_[3].setLegType(LegType::RR);

    gait_planner_.setTotalSteps(LEG_DEFAULT_TOTAL_STEPS);
    for (int i = 0; i < 4; i++) {
        legs_[i].setTotalSteps(LEG_DEFAULT_TOTAL_STEPS);
    }
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

void DogControl::clampLegsMechanical(float pos[4][LEG_JOINT_COUNT]) {
    for (int leg = 0; leg < 4; leg++) {
        legs_[leg].clampJointPositionsMechanical(pos[leg]);
    }
}

bool DogControl::ensureStandingForWalk(float max_speed_rad_s) {
    if (!deep_motor_) {
        return false;
    }
    if (state_machine_.state() == DogPoseState::Uninitialized) {
        ESP_LOGW(TAG, "行走被拒绝：请先执行整机初始化 (self.dog.init)");
        return false;
    }
    if (!state_machine_.needsStandBeforeWalk()) {
        return true;
    }
    ESP_LOGI(TAG, "当前为趴下或姿态未确认，先站立再行走");
    return stand(max_speed_rad_s);
}

void DogControl::logJointTargetsBeforeSend(const char* motion_label, const float pos[4][LEG_JOINT_COUNT]) const {
    static const char* leg_names[] = {"FL", "FR", "RL", "RR"};
    static const char* joint_names[] = {"HipAA", "HipFE", "Knee"};
    ESP_LOGI(TAG, "[关节目标] %s (rad / deg)", motion_label);
    for (int leg = 0; leg < 4; leg++) {
        ESP_LOGI(TAG, "  腿%s: %s=%.4f(%.1f°)  %s=%.4f(%.1f°)  %s=%.4f(%.1f°)",
                 leg_names[leg],
                 joint_names[0], pos[leg][0], dog_rad_to_deg(pos[leg][0]),
                 joint_names[1], pos[leg][1], dog_rad_to_deg(pos[leg][1]),
                 joint_names[2], pos[leg][2], dog_rad_to_deg(pos[leg][2]));
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
    gait_planner_.resetCycle();
    for (int i = 0; i < 4; i++) {
        legs_[i].setCurrentStep(gait_planner_.effectiveStepForLeg(i));
    }
    initialized_ = true;
    state_machine_.onInitSuccess();
    return true;
}

bool DogControl::stand(float max_speed_rad_s) {
    if (!deep_motor_) return false;
    gait_planner_.resetCycle();
    for (int i = 0; i < 4; i++) {
        legs_[i].setCurrentStep(gait_planner_.effectiveStepForLeg(i));
    }
    float pos[4][LEG_JOINT_COUNT];
    for (int leg = 0; leg < 4; leg++) {
        for (int j = 0; j < LEG_JOINT_COUNT; j++) {
            pos[leg][j] = legs_[leg].getStanceTargetJoint(j);
        }
    }
    clampLegsMechanical(pos);
    logJointTargetsBeforeSend("stand", pos);

    // 1) 12 电机统一限速（每电机 1 帧，避免原先 setPosition 每关节 2 帧）
    for (int leg = 0; leg < 4; leg++) {
        for (int j = 0; j < LEG_JOINT_COUNT; j++) {
            uint8_t id = legs_[leg].getMotorId(j);
            if (id == 0) {
                continue;
            }
            if (!deep_motor_->setMotorSpeedLimit(id, max_speed_rad_s)) {
                ESP_LOGE(TAG, "stand setMotorSpeedLimit leg=%d joint=%d id=%u", leg, j, (unsigned)id);
                return false;
            }
        }
    }
    // 2) 按关节同步：同一关节 4 条腿连续下发位置参考，再下一关节（视觉上同时抬/落）
    for (int j = 0; j < LEG_JOINT_COUNT; j++) {
        for (int leg = 0; leg < 4; leg++) {
            uint8_t id = legs_[leg].getMotorId(j);
            if (id == 0) {
                continue;
            }
            float pj = pos[leg][j];
            if (!deep_motor_->setMotorPositionRefOnly(id, pj)) {
                ESP_LOGE(TAG, "stand setMotorPositionRefOnly leg=%d joint=%d id=%u", leg, j, (unsigned)id);
                return false;
            }
        }
    }
    state_machine_.onStandSuccess();
    return true;
}

bool DogControl::lieDown(float max_speed_rad_s) {
    if (!deep_motor_) return false;
    gait_planner_.resetCycle();
    for (int i = 0; i < 4; i++) {
        legs_[i].setCurrentStep(gait_planner_.effectiveStepForLeg(i));
    }
    float pos_zero[4][LEG_JOINT_COUNT] = {};
    clampLegsMechanical(pos_zero);
    logJointTargetsBeforeSend("lie_down", pos_zero);

    for (int leg = 0; leg < 4; leg++) {
        for (int j = 0; j < LEG_JOINT_COUNT; j++) {
            uint8_t id = legs_[leg].getMotorId(j);
            if (id == 0) {
                continue;
            }
            if (!deep_motor_->setMotorSpeedLimit(id, max_speed_rad_s)) {
                ESP_LOGE(TAG, "lieDown setMotorSpeedLimit leg=%d joint=%d id=%u", leg, j, (unsigned)id);
                return false;
            }
        }
    }
    for (int j = 0; j < LEG_JOINT_COUNT; j++) {
        for (int leg = 0; leg < 4; leg++) {
            uint8_t id = legs_[leg].getMotorId(j);
            if (id == 0) {
                continue;
            }
            if (!deep_motor_->setMotorPositionRefOnly(id, pos_zero[leg][j])) {
                ESP_LOGE(TAG, "lieDown setMotorPositionRefOnly leg=%d joint=%d id=%u", leg, j, (unsigned)id);
                return false;
            }
        }
    }
    state_machine_.onLieDownSuccess();
    return true;
}

bool DogControl::goForwardStepNoEnsure(float max_speed_rad_s) {
    if (!deep_motor_) return false;
    gait_planner_.advanceCycleForward();
    float pos[4][LEG_JOINT_COUNT];
    for (int i = 0; i < 4; i++) {
        const uint16_t step_i = gait_planner_.effectiveStepForLeg(i);
        legs_[i].setCurrentStep(step_i);
        legs_[i].fillStepPositionsAtStepIndex(step_i, pos[i], true);
    }
    clampLegsMechanical(pos);
    logJointTargetsBeforeSend("go_forward", pos);

    for (int leg = 0; leg < 4; leg++) {
        for (int j = 0; j < LEG_JOINT_COUNT; j++) {
            uint8_t id = legs_[leg].getMotorId(j);
            if (id == 0) {
                continue;
            }
            if (!deep_motor_->setMotorSpeedLimit(id, max_speed_rad_s)) {
                ESP_LOGE(TAG, "goForward setMotorSpeedLimit leg=%d joint=%d", leg, j);
                return false;
            }
        }
    }
    for (int j = 0; j < LEG_JOINT_COUNT; j++) {
        for (int leg = 0; leg < 4; leg++) {
            uint8_t id = legs_[leg].getMotorId(j);
            if (id == 0) {
                continue;
            }
            if (!deep_motor_->setMotorPositionRefOnly(id, pos[leg][j])) {
                ESP_LOGE(TAG, "goForward setMotorPositionRefOnly leg=%d joint=%d", leg, j);
                return false;
            }
        }
    }
    return true;
}

bool DogControl::goForward(float max_speed_rad_s) {
    if (!ensureStandingForWalk(max_speed_rad_s)) {
        return false;
    }
    state_machine_.beginMove();
    bool ok = goForwardStepNoEnsure(max_speed_rad_s);
    state_machine_.endMove(ok);
    return ok;
}

bool DogControl::goBackStepNoEnsure(float max_speed_rad_s) {
    if (!deep_motor_) return false;
    gait_planner_.advanceCycleBackward();
    float pos[4][LEG_JOINT_COUNT];
    for (int i = 0; i < 4; i++) {
        const uint16_t step_i = gait_planner_.effectiveStepForLeg(i);
        legs_[i].setCurrentStep(step_i);
        legs_[i].fillStepPositionsAtStepIndex(step_i, pos[i], false);
    }
    clampLegsMechanical(pos);
    logJointTargetsBeforeSend("go_back", pos);

    for (int leg = 0; leg < 4; leg++) {
        for (int j = 0; j < LEG_JOINT_COUNT; j++) {
            uint8_t id = legs_[leg].getMotorId(j);
            if (id == 0) {
                continue;
            }
            if (!deep_motor_->setMotorSpeedLimit(id, max_speed_rad_s)) {
                ESP_LOGE(TAG, "goBack setMotorSpeedLimit leg=%d joint=%d", leg, j);
                return false;
            }
        }
    }
    for (int j = 0; j < LEG_JOINT_COUNT; j++) {
        for (int leg = 0; leg < 4; leg++) {
            uint8_t id = legs_[leg].getMotorId(j);
            if (id == 0) {
                continue;
            }
            if (!deep_motor_->setMotorPositionRefOnly(id, pos[leg][j])) {
                ESP_LOGE(TAG, "goBack setMotorPositionRefOnly leg=%d joint=%d", leg, j);
                return false;
            }
        }
    }
    return true;
}

bool DogControl::goBack(float max_speed_rad_s) {
    if (!ensureStandingForWalk(max_speed_rad_s)) {
        return false;
    }
    state_machine_.beginMove();
    bool ok = goBackStepNoEnsure(max_speed_rad_s);
    state_machine_.endMove(ok);
    return ok;
}

bool DogControl::goForwardSteps(int steps, float max_speed_rad_s) {
    if (!deep_motor_) {
        return false;
    }
    if (steps <= 0) {
        return true;
    }
    if (steps > 200) {
        ESP_LOGW(TAG, "goForwardSteps: steps=%d 超过 200，已截断", steps);
        steps = 200;
    }
    if (!ensureStandingForWalk(max_speed_rad_s)) {
        return false;
    }
    state_machine_.beginMove();
    for (int i = 0; i < steps; i++) {
        if (!goForwardStepNoEnsure(max_speed_rad_s)) {
            ESP_LOGE(TAG, "goForwardSteps: 第 %d/%d 步失败", i + 1, steps);
            state_machine_.endMove(false);
            return false;
        }
    }
    state_machine_.endMove(true);
    ESP_LOGI(TAG, "goForwardSteps: 完成 %d 步", steps);
    return true;
}

bool DogControl::goBackSteps(int steps, float max_speed_rad_s) {
    if (!deep_motor_) {
        return false;
    }
    if (steps <= 0) {
        return true;
    }
    if (steps > 200) {
        ESP_LOGW(TAG, "goBackSteps: steps=%d 超过 200，已截断", steps);
        steps = 200;
    }
    if (!ensureStandingForWalk(max_speed_rad_s)) {
        return false;
    }
    state_machine_.beginMove();
    for (int i = 0; i < steps; i++) {
        if (!goBackStepNoEnsure(max_speed_rad_s)) {
            ESP_LOGE(TAG, "goBackSteps: 第 %d/%d 步失败", i + 1, steps);
            state_machine_.endMove(false);
            return false;
        }
    }
    state_machine_.endMove(true);
    ESP_LOGI(TAG, "goBackSteps: 完成 %d 步", steps);
    return true;
}

bool DogControl::disable() {
    for (int i = 0; i < 4; i++) {
        if (!legs_[i].disable()) {
            ESP_LOGE(TAG, "leg %d disable failed", i);
            return false;
        }
    }
    state_machine_.onMotorSystemDisabled();
    initialized_ = false;
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

    mcp_server.AddTool("self.chassis.go_forward_n", "机器狗连续向前走 n 步（每步为一次对角步态周期）。参数 steps：步数，如 5 表示前进 5 步",
                         PropertyList(std::vector<Property>{Property("steps", kPropertyTypeInteger, 1, 1, 64)}),
                         [dog](const PropertyList& props) -> ReturnValue {
                             int n = props["steps"].value<int>();
                             if (dog->goForwardSteps(n)) {
                                 return std::string("已向前 ") + std::to_string(n) + std::string(" 步");
                             }
                             return std::string("连续前进失败");
                         });

    mcp_server.AddTool("self.chassis.go_back_n", "机器狗连续向后走 n 步。参数 steps：步数，如 6 表示后退 6 步",
                         PropertyList(std::vector<Property>{Property("steps", kPropertyTypeInteger, 1, 1, 64)}),
                         [dog](const PropertyList& props) -> ReturnValue {
                             int n = props["steps"].value<int>();
                             if (dog->goBackSteps(n)) {
                                 return std::string("已向后 ") + std::to_string(n) + std::string(" 步");
                             }
                             return std::string("连续后退失败");
                         });

    mcp_server.AddTool("self.chassis.dance", "机器狗跳舞（站立→前进四步→后退四步→站立）。用户说：跳舞、来段舞 时调用", PropertyList(), [dog](const PropertyList&) -> ReturnValue {
        if (dog->dance()) return std::string("跳舞完成");
        return std::string("跳舞失败");
    });

    ESP_LOGI(TAG, "Dog MCP tools: dog.init/stand/lie_down; chassis.go_forward/go_back/go_forward_n/go_back_n/dance");
}
