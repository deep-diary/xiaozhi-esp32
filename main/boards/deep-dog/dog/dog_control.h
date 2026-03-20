#ifndef DOG_CONTROL_H
#define DOG_CONTROL_H

#include <stdbool.h>
#include "leg/leg_control.h"

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

    /** 整机初始化：4 条腿各 3 电机使能、位置模式 */
    bool init();

    /** 整机站立：4 条腿回站立位 */
    bool stand(float max_speed_rad_s = 1.0f);

    /** 整机卧倒：4 条腿回零位 */
    bool lieDown(float max_speed_rad_s = 1.0f);

    /** 整机向前一步：4 条腿各 stepForward 一次（简单同步步态） */
    bool goForward(float max_speed_rad_s = 1.0f);

    /** 整机向后一步：4 条腿各 stepBackward 一次 */
    bool goBack(float max_speed_rad_s = 1.0f);

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
