#include "leg_control.h"
#include "motor/deep_motor.h"
#include "motor/protocol_motor.h"
#include "mcp_server.h"
#include <esp_log.h>
#include <math.h>
#include <string>
#include <cstring>

#define TAG "LegControl"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// 各腿默认电机 ID、站立位、控制上下限（与 dog/README 一致）
static const struct {
    uint8_t motor_ids[LEG_JOINT_COUNT];
    float stance[LEG_JOINT_COUNT];
    float limit_low[LEG_JOINT_COUNT];
    float limit_high[LEG_JOINT_COUNT];
} LEG_DEFAULTS[] = {
    { {11, 12, 13}, {0.0f, 0.2f, -1.16f}, {-0.2f, -0.3f, -1.86f}, {0.2f, 0.7f, -0.46f} },  // FL
    { {21, 22, 23}, {0.0f, -0.2f, 1.16f}, {-0.2f, -0.7f, 0.46f}, {0.2f, 0.3f, 1.86f} },   // FR
    { {51, 52, 53}, {0.0f, 0.2f, -1.16f}, {-0.2f, -0.3f, -1.86f}, {0.2f, 0.7f, -0.46f} },  // RL
    { {61, 62, 63}, {0.0f, -0.2f, 1.16f}, {-0.2f, -0.7f, 0.46f}, {0.2f, 0.3f, 1.86f} },   // RR
};

LegControl::LegControl() {
    memset(motor_ids_, 0, sizeof(motor_ids_));
    memset(stance_position_, 0, sizeof(stance_position_));
}

void LegControl::setLegType(LegType type) {
    leg_type_ = type;
    size_t idx = (size_t)type;
    if (idx < 4) {
        memcpy(motor_ids_, LEG_DEFAULTS[idx].motor_ids, sizeof(motor_ids_));
        memcpy(stance_position_, LEG_DEFAULTS[idx].stance, sizeof(stance_position_));
        memcpy(limit_low_, LEG_DEFAULTS[idx].limit_low, sizeof(limit_low_));
        memcpy(limit_high_, LEG_DEFAULTS[idx].limit_high, sizeof(limit_high_));
    }
    ESP_LOGI(TAG, "setLegType %d, motors %d %d %d", (int)type, motor_ids_[0], motor_ids_[1], motor_ids_[2]);
}

void LegControl::setMotorIds(uint8_t hip_aa, uint8_t hip_fe, uint8_t knee) {
    motor_ids_[LEG_JOINT_HIP_AA] = hip_aa;
    motor_ids_[LEG_JOINT_HIP_FE] = hip_fe;
    motor_ids_[LEG_JOINT_KNEE] = knee;
}

void LegControl::setLimits(const float limit_low[LEG_JOINT_COUNT], const float limit_high[LEG_JOINT_COUNT]) {
    memcpy(limit_low_, limit_low, sizeof(limit_low_));
    memcpy(limit_high_, limit_high, sizeof(limit_high_));
}

void LegControl::setStancePosition(const float stance[LEG_JOINT_COUNT]) {
    memcpy(stance_position_, stance, sizeof(stance_position_));
}

float LegControl::clampJoint(int joint_index, float value) const {
    if (joint_index < 0 || joint_index >= LEG_JOINT_COUNT) return value;
    if (value < limit_low_[joint_index]) return limit_low_[joint_index];
    if (value > limit_high_[joint_index]) return limit_high_[joint_index];
    return value;
}

void LegControl::computeStepPosition(float out_position[LEG_JOINT_COUNT], bool forward) const {
    float phase = 2.0f * (float)M_PI * (float)current_step_ / (float)total_steps_;
    float s = sinf(phase);
    bool right_leg = (leg_type_ == LegType::FR || leg_type_ == LegType::RR);
    float hip_fe_sign = right_leg ? -1.0f : 1.0f;
    float knee_sign = right_leg ? -1.0f : 1.0f;

    out_position[LEG_JOINT_HIP_AA] = clampJoint(LEG_JOINT_HIP_AA, stance_position_[LEG_JOINT_HIP_AA] + hip_aa_amp_ * s);
    out_position[LEG_JOINT_HIP_FE] = clampJoint(LEG_JOINT_HIP_FE, stance_position_[LEG_JOINT_HIP_FE] + hip_fe_sign * hip_fe_amp_ * s);
    out_position[LEG_JOINT_KNEE]   = clampJoint(LEG_JOINT_KNEE,   stance_position_[LEG_JOINT_KNEE]   + knee_sign * knee_amp_ * s);
}

bool LegControl::init() {
    if (!deep_motor_) {
        ESP_LOGE(TAG, "DeepMotor not set");
        return false;
    }
    for (int i = 0; i < LEG_JOINT_COUNT; i++) {
        uint8_t id = motor_ids_[i];
        if (id == 0) continue;
        if (!deep_motor_->isMotorRegistered(id)) {
            deep_motor_->registerMotor(id);
        }
        if (!MotorProtocol::setMotorPositionMode(id)) {
            ESP_LOGE(TAG, "setMotorPositionMode fail id=%d", id);
            return false;
        }
        if (!MotorProtocol::enableMotor(id)) {
            ESP_LOGE(TAG, "enableMotor fail id=%d", id);
            return false;
        }
    }
    ESP_LOGI(TAG, "leg init ok type=%d", (int)leg_type_);
    return true;
}

bool LegControl::disable() {
    for (int i = 0; i < LEG_JOINT_COUNT; i++) {
        if (motor_ids_[i] != 0 && !MotorProtocol::resetMotor(motor_ids_[i])) {
            ESP_LOGE(TAG, "resetMotor fail id=%d", motor_ids_[i]);
            return false;
        }
    }
    return true;
}

bool LegControl::goToZero(float max_speed_rad_s) {
    if (!deep_motor_) return false;
    for (int i = 0; i < LEG_JOINT_COUNT; i++) {
        if (motor_ids_[i] != 0 && !deep_motor_->setMotorPosition(motor_ids_[i], 0.0f, max_speed_rad_s)) {
            ESP_LOGE(TAG, "goToZero setMotorPosition fail id=%d", motor_ids_[i]);
            return false;
        }
    }
    return true;
}

bool LegControl::goToStance(float max_speed_rad_s) {
    if (!deep_motor_) return false;
    for (int i = 0; i < LEG_JOINT_COUNT; i++) {
        float pos = clampJoint(i, stance_position_[i]);
        if (motor_ids_[i] != 0 && !deep_motor_->setMotorPosition(motor_ids_[i], pos, max_speed_rad_s)) {
            ESP_LOGE(TAG, "goToStance setMotorPosition fail id=%d", motor_ids_[i]);
            return false;
        }
    }
    return true;
}

bool LegControl::stepForward(float max_speed_rad_s) {
    if (!deep_motor_) return false;
    current_step_ = (current_step_ + 1) % total_steps_;
    float pos[LEG_JOINT_COUNT];
    computeStepPosition(pos, true);
    for (int i = 0; i < LEG_JOINT_COUNT; i++) {
        if (motor_ids_[i] != 0 && !deep_motor_->setMotorPosition(motor_ids_[i], pos[i], max_speed_rad_s)) {
            ESP_LOGE(TAG, "stepForward setMotorPosition fail id=%d", motor_ids_[i]);
            return false;
        }
    }
    return true;
}

bool LegControl::stepBackward(float max_speed_rad_s) {
    if (!deep_motor_) return false;
    current_step_ = (current_step_ == 0) ? total_steps_ - 1 : current_step_ - 1;
    float pos[LEG_JOINT_COUNT];
    computeStepPosition(pos, false);
    for (int i = 0; i < LEG_JOINT_COUNT; i++) {
        if (motor_ids_[i] != 0 && !deep_motor_->setMotorPosition(motor_ids_[i], pos[i], max_speed_rad_s)) {
            ESP_LOGE(TAG, "stepBackward setMotorPosition fail id=%d", motor_ids_[i]);
            return false;
        }
    }
    return true;
}

// --- MCP 工具注册 ---

static int str_to_leg_index(const std::string& s) {
    if (s == "fl") return 0;
    if (s == "fr") return 1;
    if (s == "rl") return 2;
    if (s == "rr") return 3;
    return -1;
}

void RegisterLegMcpTools(McpServer& mcp_server, LegControl* legs[4]) {
    mcp_server.AddTool("self.leg.init", "单腿初始化（使能3个电机）", PropertyList(std::vector<Property>{
        Property("leg_id", kPropertyTypeString, std::string("fl"))
    }), [legs](const PropertyList& properties) -> ReturnValue {
        std::string lid = properties["leg_id"].value<std::string>();
        int idx = str_to_leg_index(lid);
        if (idx < 0 || !legs[idx]) return std::string("无效 leg_id 或该腿未创建，请使用 fl/fr/rl/rr");
        if (legs[idx]->init()) return std::string("腿 " + lid + " 初始化成功");
        return std::string("腿 " + lid + " 初始化失败");
    });

    mcp_server.AddTool("self.leg.stand", "单腿站立/单腿站起/单腿回站立位。用户说：腿站起来、腿站立、站起、站立位 时调用", PropertyList(std::vector<Property>{
        Property("leg_id", kPropertyTypeString, std::string("fl"))
    }), [legs](const PropertyList& properties) -> ReturnValue {
        std::string lid = properties["leg_id"].value<std::string>();
        int idx = str_to_leg_index(lid);
        if (idx < 0 || !legs[idx]) return std::string("无效 leg_id 或该腿未创建");
        if (legs[idx]->goToStance()) return std::string("腿 " + lid + " 回站立位成功");
        return std::string("腿 " + lid + " 回站立位失败");
    });

    mcp_server.AddTool("self.leg.lie_down", "单腿卧倒/单腿趴下/单腿回零位。用户说：腿卧倒、卧倒、趴下、回零位、零位、腿放平、关节回零 时调用（注意：零位是机械零位，不是灵位）", PropertyList(std::vector<Property>{
        Property("leg_id", kPropertyTypeString, std::string("fl"))
    }), [legs](const PropertyList& properties) -> ReturnValue {
        std::string lid = properties["leg_id"].value<std::string>();
        int idx = str_to_leg_index(lid);
        if (idx < 0 || !legs[idx]) return std::string("无效 leg_id 或该腿未创建");
        if (legs[idx]->goToZero()) return std::string("腿 " + lid + " 卧倒成功");
        return std::string("腿 " + lid + " 卧倒失败");
    });

    mcp_server.AddTool("self.leg.step_forward", "单腿向前迈一步", PropertyList(std::vector<Property>{
        Property("leg_id", kPropertyTypeString, std::string("fl"))
    }), [legs](const PropertyList& properties) -> ReturnValue {
        std::string lid = properties["leg_id"].value<std::string>();
        int idx = str_to_leg_index(lid);
        if (idx < 0 || !legs[idx]) return std::string("无效 leg_id 或该腿未创建");
        if (legs[idx]->stepForward()) return std::string("腿 " + lid + " 向前迈一步成功");
        return std::string("腿 " + lid + " 向前迈一步失败");
    });

    mcp_server.AddTool("self.leg.step_back", "单腿向后迈一步", PropertyList(std::vector<Property>{
        Property("leg_id", kPropertyTypeString, std::string("fl"))
    }), [legs](const PropertyList& properties) -> ReturnValue {
        std::string lid = properties["leg_id"].value<std::string>();
        int idx = str_to_leg_index(lid);
        if (idx < 0 || !legs[idx]) return std::string("无效 leg_id 或该腿未创建");
        if (legs[idx]->stepBackward()) return std::string("腿 " + lid + " 向后迈一步成功");
        return std::string("腿 " + lid + " 向后迈一步失败");
    });

    ESP_LOGI(TAG, "Leg MCP tools registered: self.leg.init, stand, lie_down, step_forward, step_back");
}
