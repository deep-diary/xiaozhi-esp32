#ifndef DOG_CONTROL_H
#define DOG_CONTROL_H

#include <stdbool.h>
#include <atomic>
#include <string>
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

    /** 连续前/后若干「小步」（每步推进一个周期采样索引，共 total_steps 步为一正弦周期） */
    bool goForwardSteps(int steps, float max_speed_rad_s = 1.0f);
    bool goBackSteps(int steps, float max_speed_rad_s = 1.0f);

    /**
     * 一大步：沿相位连续走 **半个正弦周期**（total_steps/2 个小步），小步之间可插延时。
     * 例如 total_steps=20 时一大步 = 10 个小步。
     */
    bool goForwardBigStep(float max_speed_rad_s = 1.0f, int inter_step_delay_ms = 40);
    bool goBackBigStep(float max_speed_rad_s = 1.0f, int inter_step_delay_ms = 40);

    /**
     * 持续前进/后退（后台任务循环小步，直至 stopContinuousLocomotion 或其它动作打断）。
     * step_period_ms：相邻小步之间的节拍间隔（影响「走得快慢」观感，与电机限速 speed 独立）。
     */
    bool startContinuousForward(float max_speed_rad_s = 1.0f, int step_period_ms = 80);
    bool startContinuousBackward(float max_speed_rad_s = 1.0f, int step_period_ms = 80);
    /** 持续行走中调节电机限速（rad/s） */
    void setContinuousSpeed(float max_speed_rad_s);
    /** 持续行走中调节小步间隔（ms） */
    void setContinuousStepPeriodMs(int ms);

    void stopContinuousLocomotion();
    bool isContinuousLocomotionActive() const;

    /** 供 MCP/语音反馈：中文状态摘要（含持续行走时的限速与步频） */
    std::string getChassisStatusString() const;

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
    int continuous_step_period_ms_ = 80;
    TaskHandle_t continuous_task_handle_ = nullptr;

    /** 12 关节目标统一做机械限位裁剪（README 机械列） */
    void clampLegsMechanical(float pos[4][LEG_JOINT_COUNT]);

    /** 下发前打印 12 关节目标角（rad/°），不打印 CAN 报文 */
    void logJointTargetsBeforeSend(const char* motion_label, const float pos[4][LEG_JOINT_COUNT]) const;

    /** 各腿 init 后读反馈角，核对是否在近零位（趴姿写零后应接近 0） */
    void logMotorActualPositionsAfterInit();

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
