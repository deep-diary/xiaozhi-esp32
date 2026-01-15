#ifndef _DEEP_ARM_CONTROL_H__
#define _DEEP_ARM_CONTROL_H__

#include "deep_arm.h"
#include "mcp_server.h"
#include "arm_led_state.h"

/**
 * @brief 机械臂控制MCP工具类
 * 
 * 功能：
 * 1. 将机械臂控制功能封装为MCP工具
 * 2. 提供机械臂的基本控制、录制、边界标定等功能
 * 3. 支持语音控制和远程控制
 */
class DeepArmControl {
private:
    DeepArm* deep_arm_;  // 机械臂控制器指针
    ArmLedState* arm_led_state_;  // 机械臂灯带状态管理器

public:
    /**
     * @brief 构造函数
     * @param deep_arm 机械臂控制器指针
     * @param mcp_server MCP服务器引用
     * @param led_strip 灯带控制器指针
     */
    DeepArmControl(DeepArm* deep_arm, McpServer& mcp_server, CircularStrip* led_strip);
    
    /**
     * @brief 析构函数
     */
    ~DeepArmControl();
    
    /**
     * @brief 更新机械臂状态并同步灯带效果
     */
    void UpdateArmStatus();
};

#endif // _DEEP_ARM_CONTROL_H__
