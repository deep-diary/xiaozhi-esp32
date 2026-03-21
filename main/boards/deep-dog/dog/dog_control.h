#ifndef DOG_CONTROL_H
#define DOG_CONTROL_H

#include <stdbool.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "leg/leg_control.h"
#include "gait_planner.h"
#include "dog_state_machine.h"

class DeepMotor;

/**
 * 机器狗整机控制：4 条腿、12 电机，协调站立/卧倒/前进/后退/跳舞等。
 * 内部持有 4 个 LegControl，通过 DeepMotor 下发 CAN 指令。
 */
class DogControl {
public:
    DogControl();
    ~DogControl() = default;

    /** 绑定电机管理器（必须在 init/stand/goForward 等前设置） */
    void setDeepMotor(DeepMotor* motor);
    DeepMotor* getDeepMotor() const { return deep_motor_; }

    /** 获取 4 条腿指针，用于单腿 MCP 或调试；索引 0=fl, 1=fr, 2=rl, 3=rr */
    void getLegs(LegControl* legs[4]);

    /** 步态规划（正弦相位 + 周期索引）；默认 Trot 对角步态 */
    GaitPlanner& gaitPlanner() { return gait_planner_; }
    const GaitPlanner& gaitPlanner() const { return gait_planner_; }
    void setQuadrupedGaitType(QuadrupedGaitType t) { gait_planner_.setGaitType(t); }

    /** 整机姿态状态（趴下/站立/行走中等），供板级 LED 等使用 */
    DogPoseState getPoseState() const { return state_machine_.state(); }

    /** 整机初始化：4 条腿各 3 电机使能、位置模式 */
    bool init();

    /** 整机站立：4 条腿回站立位 */
    bool stand(float max_speed_rad_s = 1.0f);

    /** 整机卧倒：4 条腿回零位 */
    bool lieDown(float max_speed_rad_s = 1.0f);

    /** 整机向前一步：推进 GaitPlanner 周期并按当前步态类型下发各腿目标（默认 Trot） */
    bool goForward(float max_speed_rad_s = 1.0f);

    /** 整机向后一步：周期反向，摆动方向取反（见 LegControl::fillStepPositionsAtStepIndex） */
    bool goBack(float max_speed_rad_s = 1.0f);

    /** 连续前/后若干步（每步一次完整 goForward/goBack） */
    bool goForwardSteps(int steps, float max_speed_rad_s = 1.0f);
    bool goBackSteps(int steps, float max_speed_rad_s = 1.0f);

    /**
     * 持续前进/后退（后台任务循环迈步，直至 stopContinuousLocomotion 或其它动作打断）。
     * 与单步 goForward/goBack 互斥；与「长按1 组合窗口」内长按 2/3 连发 5 步无关（由触摸层区分）。
     */
    bool startContinuousForward(float max_speed_rad_s = 1.0f);
    bool startContinuousBackward(float max_speed_rad_s = 1.0f);
    void stopContinuousLocomotion();
    bool isContinuousLocomotionActive() const;

    /** 整机失能：4 条腿电机 reset */
    bool disable();

    /**
     * 跳舞：简单预定义动作序列（站立 → 若干步前进 → 若干步后退 → 站立）。
     * 每步之间可加延时，后续可改为关键帧+插值。
     */
    bool dance(float max_speed_rad_s = 1.0f);

private:
    DeepMotor* deep_motor_ = nullptr;
    LegControl legs_[4];  // fl, fr, rl, rr，构造时按 LegType 配置
    GaitPlanner gait_planner_;
    DogStateMachine state_machine_;

    /** 趴下或姿态未知时先站立，再允许迈步 */
    bool ensureStandingForWalk(float max_speed_rad_s);

    bool goForwardStepNoEnsure(float max_speed_rad_s);
    bool goBackStepNoEnsure(float max_speed_rad_s);

    static void ContinuousWalkTask(void* arg);
    void ensureContinuousWalkTask();
    void stopContinuousLocomotionInternal(bool success);
    void stopContinuousLocomotionIfNeeded();

    /** 0=无 1=前进 2=后退 */
    std::atomic<uint8_t> continuous_mode_{0};
    float continuous_speed_rad_s_ = 1.0f;
    TaskHandle_t continuous_task_handle_ = nullptr;

    /** 12 关节目标统一做机械限位裁剪（README 机械列） */
    void clampLegsMechanical(float pos[4][LEG_JOINT_COUNT]);

    /** 下发前打印 12 关节目标角（rad/°），不打印 CAN 报文 */
    void logJointTargetsBeforeSend(const char* motion_label, const float pos[4][LEG_JOINT_COUNT]) const;

    // 由于初始化会给每个电机重新设置零位并使能，
    // 若重复调用可能导致机械抖动/意外转动。
    // 因此这里做一次性标记：初始化成功后只允许执行一次。
    bool initialized_ = false;
};

/**
 * 注册整机 MCP 工具：初始化、站立、卧倒、前进、后退、跳舞等。
 * 供语音/应用层调用，对应 README 中的 chassis/dog 指令。
 */
void RegisterDogMcpTools(class McpServer& mcp_server, DogControl* dog);

#endif // DOG_CONTROL_H
