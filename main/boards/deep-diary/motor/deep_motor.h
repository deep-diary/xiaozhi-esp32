#ifndef _DEEP_MOTOR_H__
#define _DEEP_MOTOR_H__

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "protocol_motor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led/circular_strip.h"
#include "deep_motor_led_state.h"

/**
 * @brief 深度电机管理类 - 用于管理多个电机的状态和ID
 * 
 * 功能：
 * 1. 自动注册和跟踪活跃的电机ID
 * 2. 解析和保存电机状态数据
 * 3. 管理当前活跃电机ID
 * 4. 提供电机状态查询接口
 */

// 最大电机数量
#define MAX_MOTOR_COUNT 8

// 电机ID未注册标识
#define MOTOR_ID_UNREGISTERED -1

// 电机反馈帧ID掩码 (0x02******)
#define MOTOR_FEEDBACK_MASK 0x02000000
#define MOTOR_FEEDBACK_MASK_SHIFT 24

// 录制功能相关常量
#define MAX_TEACHING_POINTS 300        // 最大录制点数
#define TEACHING_SAMPLE_RATE_MS 50     // 录制采样间隔(ms)
#define INIT_STATUS_RATE_MS 200       // 初始化状态查询间隔(ms)

// 宏定义用于解析29位ID
#define RX_29ID_DISASSEMBLE_MOTOR_ID(id)        (uint8_t)(((id)>>8)&0xFF)
#define RX_29ID_DISASSEMBLE_MASTER_ID(id)       (uint8_t)((id)&0xFF)

class DeepMotor {
private:
    // 已注册的电机ID列表
    int8_t registered_motor_ids_[MAX_MOTOR_COUNT];
    
    // 电机状态数组，索引对应registered_motor_ids_中的位置
    motor_status_t motor_statuses_[MAX_MOTOR_COUNT];
    
    // 电机目标位置数组，索引对应registered_motor_ids_中的位置
    float motor_target_angles_[MAX_MOTOR_COUNT];
    
    // 当前活跃电机ID
    int8_t active_motor_id_;
    
    // 已注册电机数量
    uint8_t registered_count_;
    
    // 录制功能相关变量
    bool teaching_mode_;                    // 录制模式标志
    bool teaching_data_ready_;              // 录制数据就绪标志
    float teaching_positions_[MAX_TEACHING_POINTS];  // 录制位置数据数组
    uint16_t teaching_point_count_;         // 当前录制点数
    uint16_t current_execute_index_;        // 当前执行索引
    
    // 任务句柄
    TaskHandle_t init_status_task_handle_;  // 初始化状态查询任务句柄
    TaskHandle_t teaching_task_handle_;     // 录制任务句柄
    TaskHandle_t execute_task_handle_;      // 播放任务句柄
    
    // 查找电机ID在注册列表中的索引
    int8_t findMotorIndex(uint8_t motor_id) const;
    
    // 注册新电机ID
    bool registerMotorId(uint8_t motor_id);
    
    // 静态任务函数
    static void initStatusTask(void* parameter);
    static void recordingTask(void* parameter);
    static void playTask(void* parameter);
    
    // 回调函数类型定义
    typedef void (*MotorDataCallback)(uint8_t motor_id, float position, void* user_data);
    
    // 回调函数相关
    MotorDataCallback data_callback_;
    void* callback_user_data_;
    
    // LED状态管理器
    DeepMotorLedState* led_state_manager_;

public:
    /**
     * @brief 构造函数
     * @param led_strip LED灯带控制器指针，如果为nullptr则不启用LED功能
     */
    DeepMotor(CircularStrip* led_strip = nullptr);
    
    /**
     * @brief 析构函数
     */
    ~DeepMotor();
    
    /**
     * @brief 处理CAN帧，自动注册电机并更新状态
     * @param can_frame 接收到的CAN帧
     * @return true 如果是电机反馈帧并处理成功, false 否则
     */
    bool processCanFrame(const CanFrame& can_frame);
    
    /**
     * @brief 获取当前活跃电机ID
     * @return 活跃电机ID，-1表示无活跃电机
     */
    int8_t getActiveMotorId() const;
    
    /**
     * @brief 设置当前活跃电机ID
     * @param motor_id 电机ID
     * @return true 成功, false 失败（电机未注册）
     */
    bool setActiveMotorId(uint8_t motor_id);
    
    /**
     * @brief 获取电机状态
     * @param motor_id 电机ID
     * @param status 状态结构体指针
     * @return true 成功, false 失败（电机未注册）
     */
    bool getMotorStatus(uint8_t motor_id, motor_status_t* status) const;
    
    /**
     * @brief 设置电机目标位置
     * @param motor_id 电机ID
     * @param target_angle 目标角度（弧度）
     * @return true 成功, false 失败（电机未注册）
     */
    bool setMotorTargetAngle(uint8_t motor_id, float target_angle);
    
    /**
     * @brief 获取电机目标位置
     * @param motor_id 电机ID
     * @param target_angle 目标角度输出指针
     * @return true 成功, false 失败（电机未注册）
     */
    bool getMotorTargetAngle(uint8_t motor_id, float* target_angle) const;
    
    /**
     * @brief 设置电机位置（包装方法，同时更新目标位置）
     * @param motor_id 电机ID
     * @param position 目标位置（弧度）
     * @param max_speed 最大速度（弧度/秒）
     * @return true 成功, false 失败
     */
    bool setMotorPosition(uint8_t motor_id, float position, float max_speed = 30.0f);
    
    /**
     * @brief 获取所有已注册电机ID
     * @param motor_ids 输出数组，用于存储电机ID
     * @param max_count 数组最大长度
     * @return 实际返回的电机数量
     */
    uint8_t getRegisteredMotorIds(int8_t* motor_ids, uint8_t max_count) const;
    
    /**
     * @brief 检查电机是否已注册
     * @param motor_id 电机ID
     * @return true 已注册, false 未注册
     */
    bool isMotorRegistered(uint8_t motor_id) const;
    
    /**
     * @brief 获取已注册电机数量
     * @return 已注册电机数量
     */
    uint8_t getRegisteredCount() const;
    
    /**
     * @brief 清除所有电机注册信息
     */
    void clearAllMotors();
    
    /**
     * @brief 打印所有电机状态信息（调试用）
     */
    void printAllMotorStatus() const;
    
    /**
     * @brief 开始录制模式
     * @param motor_id 电机ID
     * @return true 成功, false 失败
     * 
     * 功能：
     * 1. 停止电机（便于人工拖动）
     * 2. 启动50ms定时任务发送位置查询
     * 3. 设置录制标志位
     * 4. 清空录制数据数组
     */
    bool startTeaching(uint8_t motor_id);
    
    /**
     * @brief 结束录制模式
     * @return true 成功, false 失败
     * 
     * 功能：
     * 1. 终止录制任务
     * 2. 清除录制标志位
     * 3. 保存录制数据
     */
    bool stopTeaching();
    
    /**
     * @brief 播放录制
     * @param motor_id 电机ID
     * @return true 成功, false 失败
     * 
     * 功能：
     * 1. 使能电机
     * 2. 每隔50ms发送位置数据
     * 3. 按顺序发送录制数据
     * 4. 发送完毕后停止
     */
    bool executeTeaching(uint8_t motor_id);
    
    /**
     * @brief 获取录制状态
     * @return true 录制模式中, false 非录制模式
     */
    bool isTeachingMode() const;
    
    /**
     * @brief 获取录制数据是否就绪
     * @return true 录制数据就绪, false 录制数据未就绪
     */
    bool isTeachingDataReady() const;
    
    /**
     * @brief 获取录制点数
     * @return 录制点数
     */
    uint16_t getTeachingPointCount() const;
    
    /**
     * @brief 启动初始化状态查询任务
     * @param motor_id 电机ID
     * @return true 成功, false 失败
     * 
     * 功能：
     * 1. 启动1秒定时任务发送位置模式查询
     * 2. 便于获取电机最新状态信息
     */
    bool startInitStatusTask(uint8_t motor_id);
    
    /**
     * @brief 停止初始化状态查询任务
     * @return true 成功, false 失败
     */
    bool stopInitStatusTask();
    
    /**
     * @brief 设置电机数据回调函数
     * @param callback 回调函数指针
     * @param user_data 用户数据指针
     */
    void setMotorDataCallback(MotorDataCallback callback, void* user_data = nullptr);
    
    // ========== LED控制接口 ==========
    
    /**
     * @brief 启用电机角度LED指示器
     * @param motor_id 电机ID
     * @param enabled 是否启用
     * @return true 成功, false 失败
     */
    bool enableAngleIndicator(uint8_t motor_id, bool enabled = true);
    
    /**
     * @brief 禁用电机角度LED指示器
     * @param motor_id 电机ID
     * @return true 成功, false 失败
     */
    bool disableAngleIndicator(uint8_t motor_id);
    
    /**
     * @brief 设置电机角度范围
     * @param motor_id 电机ID
     * @param min_angle 最小角度（弧度）
     * @param max_angle 最大角度（弧度）
     * @return true 成功, false 失败
     */
    bool setAngleRange(uint8_t motor_id, float min_angle, float max_angle);
    
    /**
     * @brief 获取电机角度状态
     * @param motor_id 电机ID
     * @return 电机角度状态
     */
    DeepMotorLedState::MotorAngleState getAngleStatus(uint8_t motor_id) const;
    
    /**
     * @brief 获取角度范围
     * @param motor_id 电机ID
     * @return 角度范围
     */
    DeepMotorLedState::AngleRange getAngleRange(uint8_t motor_id) const;
    
    /**
     * @brief 检查角度指示器是否启用
     * @param motor_id 电机ID
     * @return true 启用, false 未启用
     */
    bool isAngleIndicatorEnabled(uint8_t motor_id) const;
    
    /**
     * @brief 停止所有角度指示器
     */
    void stopAllAngleIndicators();
    
    /**
     * @brief 获取LED状态管理器指针（用于高级控制）
     * @return LED状态管理器指针，如果未初始化则返回nullptr
     */
    DeepMotorLedState* getLedStateManager() const;

    // ========== 呼吸灯控制接口 ==========
    
    /**
     * @brief 启用电机呼吸灯效果（正弦信号时使用）
     * @param motor_id 电机ID
     * @param red 红色分量 (0-255)
     * @param green 绿色分量 (0-255)
     * @param blue 蓝色分量 (0-255)
     * @return true 成功, false 失败
     */
    bool enableBreatheEffect(uint8_t motor_id, uint8_t red, uint8_t green, uint8_t blue);
    
    /**
     * @brief 禁用电机呼吸灯效果
     * @param motor_id 电机ID
     * @return true 成功, false 失败
     */
    bool disableBreatheEffect(uint8_t motor_id);
    
    /**
     * @brief 检查电机是否在呼吸灯模式
     * @param motor_id 电机ID
     * @return true 在呼吸灯模式, false 不在
     */
    bool isBreatheEffectEnabled(uint8_t motor_id) const;

    // ========== 软件版本查询接口 ==========
    
    /**
     * @brief 获取电机软件版本号
     * @param motor_id 电机ID
     * @param version 版本号字符串缓冲区
     * @param buffer_size 缓冲区大小
     * @return true 成功, false 失败（电机未注册）
     */
    bool getMotorSoftwareVersion(uint8_t motor_id, char* version, size_t buffer_size) const;
};

#endif // _DEEP_MOTOR_H__
