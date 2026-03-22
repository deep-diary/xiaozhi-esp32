#include "dog_control.h"
#include "motor/deep_motor.h"
#include "mcp_server.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static bool IsContinuousWalkingPose(DogPoseState s) {
    return s == DogPoseState::WalkingForward || s == DogPoseState::WalkingBackward;
}

#define TAG "DogControl"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

static inline float dog_rad_to_deg(float rad) {
    return rad * 180.0f / (float)M_PI;
}

/** MCP 整型速度：30~250 表示 0.30~2.50 rad/s（÷100） */
static float McpSpeedIntToRad(int speed_x100) {
    if (speed_x100 < 30) {
        speed_x100 = 30;
    }
    if (speed_x100 > 250) {
        speed_x100 = 250;
    }
    return speed_x100 / 100.0f;
}

static int ClampInt(int v, int lo, int hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
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
    if (IsContinuousWalkingPose(state_machine_.state())) {
        return true;
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
    // 等待若干反馈帧，再读实际角（否则可能读到旧缓存）
    vTaskDelay(pdMS_TO_TICKS(150));
    logMotorActualPositionsAfterInit();
    return true;
}

void DogControl::logMotorActualPositionsAfterInit() {
    if (!deep_motor_) {
        return;
    }
    static const char* leg_names[] = {"FL", "FR", "RL", "RR"};
    static const char* joint_names[] = {"HipAA", "HipFE", "Knee"};
    constexpr float kZeroTolRad = 0.15f;  // ~8.6°，趴姿写零后应落在此内
    ESP_LOGI(TAG, "[init 后反馈] 各电机实际角（宜在零位附近），容差 ±%.3f rad", kZeroTolRad);
    int abnormal = 0;
    for (int leg = 0; leg < 4; leg++) {
        for (int j = 0; j < LEG_JOINT_COUNT; j++) {
            const uint8_t id = legs_[leg].getMotorId(j);
            if (id == 0) {
                continue;
            }
            float rad = 0.f;
            if (!deep_motor_->getMotorActualPosition(id, &rad)) {
                ESP_LOGW(TAG, "  %s.%s id=%u: 无法读取位置（未注册或无反馈）", leg_names[leg], joint_names[j],
                         (unsigned)id);
                abnormal++;
                continue;
            }
            const float deg = dog_rad_to_deg(rad);
            if (std::fabs(rad) > kZeroTolRad) {
                ESP_LOGW(TAG, "  %s.%s id=%u: pos=%.4f rad (%.1f°) **偏离零位**", leg_names[leg], joint_names[j],
                         (unsigned)id, rad, deg);
                abnormal++;
            } else {
                ESP_LOGI(TAG, "  %s.%s id=%u: pos=%.4f rad (%.1f°) ok", leg_names[leg], joint_names[j],
                         (unsigned)id, rad, deg);
            }
        }
    }
    if (abnormal > 0) {
        ESP_LOGW(TAG, "[init 后反馈] 共 %d 项异常或不可读，请检查趴姿机械零位、CAN 与驱动反馈", abnormal);
    } else {
        ESP_LOGI(TAG, "[init 后反馈] 可读关节均在 ±%.3f rad 内，置零外观正常", kZeroTolRad);
    }
}

bool DogControl::stand(float max_speed_rad_s) {
    if (!deep_motor_) return false;
    stopContinuousLocomotionIfNeeded();
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
    stopContinuousLocomotionIfNeeded();
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
    if (IsContinuousWalkingPose(state_machine_.state())) {
        ESP_LOGW(TAG, "单步前进被拒绝：当前为持续前进/后退，请先停止");
        return false;
    }
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
    if (IsContinuousWalkingPose(state_machine_.state())) {
        ESP_LOGW(TAG, "单步后退被拒绝：当前为持续前进/后退，请先停止");
        return false;
    }
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
    if (IsContinuousWalkingPose(state_machine_.state())) {
        ESP_LOGW(TAG, "goForwardSteps 被拒绝：当前为持续前进/后退，请先停止");
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
    if (IsContinuousWalkingPose(state_machine_.state())) {
        ESP_LOGW(TAG, "goBackSteps 被拒绝：当前为持续前进/后退，请先停止");
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

bool DogControl::goForwardBigStep(float max_speed_rad_s, int inter_step_delay_ms) {
    if (!deep_motor_) {
        return false;
    }
    if (IsContinuousWalkingPose(state_machine_.state())) {
        ESP_LOGW(TAG, "前进一大步被拒绝：当前为持续前进/后退，请先停止");
        return false;
    }
    const uint16_t total = gait_planner_.getTotalSteps();
    if (total < 4) {
        return false;
    }
    const int n = total / 2;
    if (n <= 0) {
        return false;
    }
    inter_step_delay_ms = ClampInt(inter_step_delay_ms, 0, 300);
    if (!ensureStandingForWalk(max_speed_rad_s)) {
        return false;
    }
    state_machine_.beginMove();
    for (int i = 0; i < n; i++) {
        if (!goForwardStepNoEnsure(max_speed_rad_s)) {
            ESP_LOGE(TAG, "goForwardBigStep: 第 %d/%d 小步失败", i + 1, n);
            state_machine_.endMove(false);
            return false;
        }
        if (i + 1 < n && inter_step_delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(inter_step_delay_ms));
        }
    }
    state_machine_.endMove(true);
    ESP_LOGI(TAG, "goForwardBigStep: 完成一大步（%d 小步，半周期）", n);
    return true;
}

bool DogControl::goBackBigStep(float max_speed_rad_s, int inter_step_delay_ms) {
    if (!deep_motor_) {
        return false;
    }
    if (IsContinuousWalkingPose(state_machine_.state())) {
        ESP_LOGW(TAG, "后退一大步被拒绝：当前为持续前进/后退，请先停止");
        return false;
    }
    const uint16_t total = gait_planner_.getTotalSteps();
    if (total < 4) {
        return false;
    }
    const int n = total / 2;
    if (n <= 0) {
        return false;
    }
    inter_step_delay_ms = ClampInt(inter_step_delay_ms, 0, 300);
    if (!ensureStandingForWalk(max_speed_rad_s)) {
        return false;
    }
    state_machine_.beginMove();
    for (int i = 0; i < n; i++) {
        if (!goBackStepNoEnsure(max_speed_rad_s)) {
            ESP_LOGE(TAG, "goBackBigStep: 第 %d/%d 小步失败", i + 1, n);
            state_machine_.endMove(false);
            return false;
        }
        if (i + 1 < n && inter_step_delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(inter_step_delay_ms));
        }
    }
    state_machine_.endMove(true);
    ESP_LOGI(TAG, "goBackBigStep: 完成一大步（%d 小步，半周期）", n);
    return true;
}

void DogControl::setContinuousSpeed(float max_speed_rad_s) {
    if (max_speed_rad_s < 0.2f) {
        max_speed_rad_s = 0.2f;
    }
    if (max_speed_rad_s > 2.5f) {
        max_speed_rad_s = 2.5f;
    }
    continuous_speed_rad_s_ = max_speed_rad_s;
    ESP_LOGI(TAG, "持续行走电机限速设为 %.2f rad/s", max_speed_rad_s);
}

void DogControl::setContinuousStepPeriodMs(int ms) {
    continuous_step_period_ms_ = ClampInt(ms, 30, 250);
    ESP_LOGI(TAG, "持续行走步频周期设为 %d ms", continuous_step_period_ms_);
}

std::string DogControl::getChassisStatusString() const {
    const DogPoseState s = state_machine_.state();
    const char* pose = "未知";
    switch (s) {
        case DogPoseState::Uninitialized:
            pose = "未初始化";
            break;
        case DogPoseState::UnknownPose:
            pose = "姿态未知";
            break;
        case DogPoseState::Lying:
            pose = "趴下待机";
            break;
        case DogPoseState::Standing:
            pose = "站立";
            break;
        case DogPoseState::Moving:
            pose = "单段行走中";
            break;
        case DogPoseState::WalkingForward:
            pose = "持续前进中";
            break;
        case DogPoseState::WalkingBackward:
            pose = "持续后退中";
            break;
        default:
            break;
    }
    std::string out = std::string("机器狗状态：") + pose;
    if (IsContinuousWalkingPose(s)) {
        char extra[128];
        snprintf(extra, sizeof(extra),
                 "；电机限速 %.2f rad/s；小步间隔 %d ms（共 %u 步/正弦周期）",
                 continuous_speed_rad_s_, continuous_step_period_ms_,
                 (unsigned)gait_planner_.getTotalSteps());
        out += extra;
    } else {
        char extra[96];
        snprintf(extra, sizeof(extra), "；步态周期采样数=%u", (unsigned)gait_planner_.getTotalSteps());
        out += extra;
    }
    return out;
}

void DogControl::ensureContinuousWalkTask() {
    if (continuous_task_handle_) {
        return;
    }
    const BaseType_t ok =
        xTaskCreate(ContinuousWalkTask, "dog_cont_walk", 4096, this, 5, &continuous_task_handle_);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "创建连续行走任务失败");
        continuous_task_handle_ = nullptr;
    }
}

void DogControl::ContinuousWalkTask(void* arg) {
    DogControl* self = static_cast<DogControl*>(arg);
    if (!self) {
        vTaskDelete(nullptr);
        return;
    }
    const TickType_t idle_delay = pdMS_TO_TICKS(30);
    for (;;) {
        const uint8_t mode = self->continuous_mode_.load(std::memory_order_relaxed);
        const int period_ms = self->continuous_step_period_ms_ > 0 ? self->continuous_step_period_ms_ : 80;
        const TickType_t step_delay = pdMS_TO_TICKS(period_ms);
        if (mode == 1) {
            if (!self->goForwardStepNoEnsure(self->continuous_speed_rad_s_)) {
                self->stopContinuousLocomotionInternal(false);
            }
            vTaskDelay(step_delay);
        } else if (mode == 2) {
            if (!self->goBackStepNoEnsure(self->continuous_speed_rad_s_)) {
                self->stopContinuousLocomotionInternal(false);
            }
            vTaskDelay(step_delay);
        } else {
            vTaskDelay(idle_delay);
        }
    }
}

void DogControl::stopContinuousLocomotionInternal(bool success) {
    continuous_mode_.store(0, std::memory_order_relaxed);
    state_machine_.endContinuousLocomotion(success);
}

void DogControl::stopContinuousLocomotionIfNeeded() {
    if (IsContinuousWalkingPose(state_machine_.state()) || continuous_mode_.load(std::memory_order_relaxed) != 0) {
        stopContinuousLocomotionInternal(true);
    }
}

void DogControl::stopContinuousLocomotion() {
    stopContinuousLocomotionIfNeeded();
}

bool DogControl::isContinuousLocomotionActive() const {
    return IsContinuousWalkingPose(state_machine_.state());
}

bool DogControl::startContinuousForward(float max_speed_rad_s, int step_period_ms) {
    if (!deep_motor_) {
        return false;
    }
    setContinuousSpeed(max_speed_rad_s);
    continuous_step_period_ms_ = ClampInt(step_period_ms, 30, 250);
    ensureContinuousWalkTask();
    if (!continuous_task_handle_) {
        return false;
    }
    DogPoseState s = state_machine_.state();
    if (s == DogPoseState::WalkingForward) {
        ESP_LOGI(TAG, "持续前进参数已更新");
        return true;
    }
    if (s == DogPoseState::WalkingBackward) {
        stopContinuousLocomotionInternal(true);
        s = state_machine_.state();
    }
    if (s != DogPoseState::Standing) {
        ESP_LOGW(TAG, "持续前进被拒绝：需先站立（当前非站立/持续态）");
        return false;
    }
    if (!ensureStandingForWalk(max_speed_rad_s)) {
        return false;
    }
    if (state_machine_.state() != DogPoseState::Standing) {
        ESP_LOGW(TAG, "持续前进被拒绝：未能进入站立");
        return false;
    }
    state_machine_.beginContinuousForward();
    continuous_mode_.store(1, std::memory_order_relaxed);
    ESP_LOGI(TAG, "持续前进已启动（限速 %.2f rad/s，间隔 %d ms）", continuous_speed_rad_s_, continuous_step_period_ms_);
    return true;
}

bool DogControl::startContinuousBackward(float max_speed_rad_s, int step_period_ms) {
    if (!deep_motor_) {
        return false;
    }
    setContinuousSpeed(max_speed_rad_s);
    continuous_step_period_ms_ = ClampInt(step_period_ms, 30, 250);
    ensureContinuousWalkTask();
    if (!continuous_task_handle_) {
        return false;
    }
    DogPoseState s = state_machine_.state();
    if (s == DogPoseState::WalkingBackward) {
        ESP_LOGI(TAG, "持续后退参数已更新");
        return true;
    }
    if (s == DogPoseState::WalkingForward) {
        stopContinuousLocomotionInternal(true);
        s = state_machine_.state();
    }
    if (s != DogPoseState::Standing) {
        ESP_LOGW(TAG, "持续后退被拒绝：需先站立（当前非站立/持续态）");
        return false;
    }
    if (!ensureStandingForWalk(max_speed_rad_s)) {
        return false;
    }
    if (state_machine_.state() != DogPoseState::Standing) {
        ESP_LOGW(TAG, "持续后退被拒绝：未能进入站立");
        return false;
    }
    state_machine_.beginContinuousBackward();
    continuous_mode_.store(2, std::memory_order_relaxed);
    ESP_LOGI(TAG, "持续后退已启动（限速 %.2f rad/s，间隔 %d ms）", continuous_speed_rad_s_, continuous_step_period_ms_);
    return true;
}

bool DogControl::disable() {
    stopContinuousLocomotionIfNeeded();
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
    if (!stand(max_speed_rad_s)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(800));
    const int stride_delay_ms = 45;
    // 各 2 次「一大步」（半正弦周期串联），替代原先 4×小步 + 4×小步，动作更完整、总时长相近
    for (int i = 0; i < 2; i++) {
        if (!goForwardBigStep(max_speed_rad_s, stride_delay_ms)) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(450));
    }
    for (int i = 0; i < 2; i++) {
        if (!goBackBigStep(max_speed_rad_s, stride_delay_ms)) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(450));
    }
    if (!stand(max_speed_rad_s)) {
        return false;
    }
    ESP_LOGI(TAG, "dance done (big-step stride x2 forward + x2 back)");
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

    mcp_server.AddTool("self.chassis.forward_big",
                         "机器狗向前「一大步」：沿相位连续迈半个正弦周期（若干小步+可选延时），非持续行走。参数 speed：30~250 表示 0.30~2.50 rad/s；step_delay_ms：小步之间延时 0~300ms",
                         PropertyList(std::vector<Property>{
                             Property("speed", kPropertyTypeInteger, 100, 30, 250),
                             Property("step_delay_ms", kPropertyTypeInteger, 40, 0, 300)}),
                         [dog](const PropertyList& props) -> ReturnValue {
                             const float sp = McpSpeedIntToRad(props["speed"].value<int>());
                             const int d = props["step_delay_ms"].value<int>();
                             if (dog->goForwardBigStep(sp, d)) {
                                 return std::string("已向前一大步");
                             }
                             return std::string("前进一大步失败");
                         });

    mcp_server.AddTool("self.chassis.backward_big",
                         "机器狗向后「一大步」：半个正弦周期的小步串联。参数 speed、step_delay_ms 同 forward_big",
                         PropertyList(std::vector<Property>{
                             Property("speed", kPropertyTypeInteger, 100, 30, 250),
                             Property("step_delay_ms", kPropertyTypeInteger, 40, 0, 300)}),
                         [dog](const PropertyList& props) -> ReturnValue {
                             const float sp = McpSpeedIntToRad(props["speed"].value<int>());
                             const int d = props["step_delay_ms"].value<int>();
                             if (dog->goBackBigStep(sp, d)) {
                                 return std::string("已向后一大步");
                             }
                             return std::string("后退一大步失败");
                         });

    mcp_server.AddTool("self.chassis.start_forward",
                         "机器狗持续向前行走，直到 stop。参数 speed：电机限速(数值÷100=rad/s，如100=1.0)；step_period_ms：小步间隔30~250，越小步频越快（跑得快一点可略减小间隔或提高 speed）",
                         PropertyList(std::vector<Property>{
                             Property("speed", kPropertyTypeInteger, 100, 30, 250),
                             Property("step_period_ms", kPropertyTypeInteger, 80, 30, 250)}),
                         [dog](const PropertyList& props) -> ReturnValue {
                             const float sp = McpSpeedIntToRad(props["speed"].value<int>());
                             const int p = props["step_period_ms"].value<int>();
                             if (dog->startContinuousForward(sp, p)) {
                                 return std::string("已开始持续前进");
                             }
                             return std::string("持续前进启动失败");
                         });

    mcp_server.AddTool("self.chassis.start_backward",
                         "机器狗持续向后退，直到 stop。参数同 start_forward",
                         PropertyList(std::vector<Property>{
                             Property("speed", kPropertyTypeInteger, 100, 30, 250),
                             Property("step_period_ms", kPropertyTypeInteger, 80, 30, 250)}),
                         [dog](const PropertyList& props) -> ReturnValue {
                             const float sp = McpSpeedIntToRad(props["speed"].value<int>());
                             const int p = props["step_period_ms"].value<int>();
                             if (dog->startContinuousBackward(sp, p)) {
                                 return std::string("已开始持续后退");
                             }
                             return std::string("持续后退启动失败");
                         });

    mcp_server.AddTool("self.chassis.stop",
                         "停止持续前进/后退，关节保持当前姿态。用户说：停下、停止、别走了 时调用",
                         PropertyList(),
                         [dog](const PropertyList&) -> ReturnValue {
                             dog->stopContinuousLocomotion();
                             return std::string("已停止持续行走");
                         });

    mcp_server.AddTool("self.chassis.status",
                         "查询机器狗当前姿态与行走状态（中文）",
                         PropertyList(),
                         [dog](const PropertyList&) -> ReturnValue {
                             return dog->getChassisStatusString();
                         });

    mcp_server.AddTool("self.chassis.set_speed",
                         "调节持续行走的电机限速与小步间隔（跑快/慢：提高或降低 speed；步频：减小或增大 step_period_ms）。未在持续行走时也会作为下次 start 的默认参数",
                         PropertyList(std::vector<Property>{
                             Property("speed", kPropertyTypeInteger, 100, 30, 250),
                             Property("step_period_ms", kPropertyTypeInteger, 80, 30, 250)}),
                         [dog](const PropertyList& props) -> ReturnValue {
                             const float sp = McpSpeedIntToRad(props["speed"].value<int>());
                             const int p = props["step_period_ms"].value<int>();
                             dog->setContinuousSpeed(sp);
                             dog->setContinuousStepPeriodMs(p);
                             return std::string("已更新：限速约 ") + std::to_string(props["speed"].value<int>()) +
                                    "/100 rad/s，小步间隔 " + std::to_string(p) + "ms";
                         });

    mcp_server.AddTool("self.chassis.dance", "机器狗跳舞（站立→2次前进一大步→2次后退一大步→站立）。用户说：跳舞、来段舞 时调用", PropertyList(), [dog](const PropertyList&) -> ReturnValue {
        if (dog->dance()) return std::string("跳舞完成");
        return std::string("跳舞失败");
    });

    ESP_LOGI(TAG,
             "Dog MCP: dog.init/stand/lie_down; chassis.forward_big/backward_big/start_forward/start_backward/stop/status/"
             "set_speed/dance");
}
