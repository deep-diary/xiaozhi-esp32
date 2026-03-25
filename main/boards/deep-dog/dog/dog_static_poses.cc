#include "dog_static_poses.h"

static inline void FillStandFromLegs(const LegControl legs[4], float out[4][LEG_JOINT_COUNT]) {
    for (int leg = 0; leg < 4; leg++) {
        for (int j = 0; j < LEG_JOINT_COUNT; j++) {
            out[leg][j] = legs[leg].getStanceTargetJoint(j);
        }
    }
}

static inline void FillZeros(float out[4][LEG_JOINT_COUNT]) {
    for (int leg = 0; leg < 4; leg++) {
        for (int j = 0; j < LEG_JOINT_COUNT; j++) {
            out[leg][j] = 0.0f;
        }
    }
}

const char* DogStaticPoseName(DogStaticPoseId id) {
    switch (id) {
        case DogStaticPoseId::LieDownZero:     return "lie_down_zero";
        case DogStaticPoseId::Stand:           return "stand";
        case DogStaticPoseId::TiltLeft:        return "tilt_left";
        case DogStaticPoseId::TiltRight:       return "tilt_right";
        case DogStaticPoseId::FrontDownBackUp: return "front_down_back_up";
        case DogStaticPoseId::FrontUpBackDown: return "front_up_back_down";
        default:                               return "unknown_pose";
    }
}

void FillDogStaticPose(DogStaticPoseId id, const LegControl legs[4], float out[4][LEG_JOINT_COUNT]) {
    // 基准：站立位
    float base[4][LEG_JOINT_COUNT] = {};
    FillStandFromLegs(legs, base);

    // 默认输出为站立位
    for (int leg = 0; leg < 4; leg++) {
        for (int j = 0; j < LEG_JOINT_COUNT; j++) {
            out[leg][j] = base[leg][j];
        }
    }

    if (id == DogStaticPoseId::Stand) {
        return;
    }
    if (id == DogStaticPoseId::LieDownZero) {
        FillZeros(out);
        return;
    }

    // 下面所有静态点都在“行走范围附近的小幅偏置”，避免一上来就撞机械限位。
    // 单位：rad
    const float aa = 0.14f;     // 髋侧摆偏置幅度（左右倾）
    const float fe = 0.10f;     // 髋前后偏置（俯仰）
    const float knee = 0.30f;   // 膝偏置（伸长/缩短腿长，控制前后俯仰体感）

    // leg index: 0=FL,1=FR,2=RL,3=RR
    const bool is_front[4] = {true, true, false, false};
    const bool is_left[4] = {true, false, true, false};

    for (int leg = 0; leg < 4; leg++) {
        switch (id) {
            case DogStaticPoseId::TiltLeft: {
                // 目标：身体向左“压”——左侧两腿更靠内、右侧两腿更靠外（基于各腿 AA 正负定义）。
                // 左腿：AA +aa；右腿：AA -aa（对称写法，不依赖“内/外”文案）
                out[leg][LEG_JOINT_HIP_AA] = base[leg][LEG_JOINT_HIP_AA] + (is_left[leg] ? +aa : -aa);
            } break;
            case DogStaticPoseId::TiltRight: {
                out[leg][LEG_JOINT_HIP_AA] = base[leg][LEG_JOINT_HIP_AA] + (is_left[leg] ? -aa : +aa);
            } break;
            case DogStaticPoseId::FrontDownBackUp: {
                // 前趴后仰：前腿缩短（膝更弯，接近 0），后腿伸长（膝更直，远离 0）
                // 同时给一点髋前后偏置，增强观感
                if (is_front[leg]) {
                    out[leg][LEG_JOINT_HIP_FE] = base[leg][LEG_JOINT_HIP_FE] + fe;
                    out[leg][LEG_JOINT_KNEE]   = base[leg][LEG_JOINT_KNEE] + (base[leg][LEG_JOINT_KNEE] >= 0.0f ? -knee : +knee);
                } else {
                    out[leg][LEG_JOINT_HIP_FE] = base[leg][LEG_JOINT_HIP_FE] - fe;
                    out[leg][LEG_JOINT_KNEE]   = base[leg][LEG_JOINT_KNEE] + (base[leg][LEG_JOINT_KNEE] >= 0.0f ? +knee : -knee);
                }
            } break;
            case DogStaticPoseId::FrontUpBackDown: {
                // 前仰后趴：前腿伸长，后腿缩短
                if (is_front[leg]) {
                    out[leg][LEG_JOINT_HIP_FE] = base[leg][LEG_JOINT_HIP_FE] - fe;
                    out[leg][LEG_JOINT_KNEE]   = base[leg][LEG_JOINT_KNEE] + (base[leg][LEG_JOINT_KNEE] >= 0.0f ? +knee : -knee);
                } else {
                    out[leg][LEG_JOINT_HIP_FE] = base[leg][LEG_JOINT_HIP_FE] + fe;
                    out[leg][LEG_JOINT_KNEE]   = base[leg][LEG_JOINT_KNEE] + (base[leg][LEG_JOINT_KNEE] >= 0.0f ? -knee : +knee);
                }
            } break;
            default:
                break;
        }
    }
}

