#ifndef _DEEP_ARM_H__
#define _DEEP_ARM_H__

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "../motor/deep_motor.h"
#include "../motor/protocol_motor.h"
#include "settings.h"
#include "trajectory_planner.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * @brief 机械臂控制类 - 管理6个电机的协调控制
 * 
 * 功能：
 * 1. 6个电机的统一控制（零位、启动、关闭、初始化）
 * 2. 机械臂录制功能（同时录制6个电机的动作）
 * 3. 边界位置标定（软件限位）
 * 4. 极限位置数据持久化存储
 * 5. 状态查询和边界查询任务
 */

// 机械臂配置常量
#define ARM_MOTOR_COUNT 6                    // 机械臂电机数量
#define MAX_RECORDING_POINTS MAX_ORIGINAL_POINTS             // 最大录制点数
#define RECORDING_SAMPLE_RATE_MS 100           // 录制采样间隔(ms)
#define STATUS_QUERY_RATE_MS 1000            // 状态查询间隔(ms)
#define BOUNDARY_QUERY_RATE_MS 500           // 边界查询间隔(ms)

// 录制控制常量
#define MAX_RECORDING_TIME_MS 5000          // 最长录制时间(ms) - 10秒
#define RECORDING_WAIT_THRESHOLD_MS 2000     // 录制结束等待阈值(ms) - 2秒
#define MAX_RECORDING_POINTS_CALCULATED ((MAX_RECORDING_TIME_MS / RECORDING_SAMPLE_RATE_MS) > MAX_RECORDING_POINTS ? MAX_RECORDING_POINTS : (MAX_RECORDING_TIME_MS / RECORDING_SAMPLE_RATE_MS)) // 根据时间计算的最大点数，但不超过MAX_RECORDING_POINTS

// 轨迹规划控制宏
#define ENABLE_TRAJECTORY_EXECUTION true   // true: 实际执行轨迹, false: 仅打印轨迹点

// 边界标定状态
typedef enum {
    BOUNDARY_NOT_CALIBRATED = 0,             // 未标定
    BOUNDARY_CALIBRATING = 1,                // 标定中
    BOUNDARY_CALIBRATED = 2                  // 已标定
} boundary_calibration_status_t;

    // 机械臂状态
typedef struct {
    bool is_initialized;                     // 是否已初始化
    bool is_enabled;                         // 是否已启动
    bool is_recording;                       // 是否正在录制
    bool recording_data_ready;               // 录制数据是否就绪
    bool is_playing;                         // 是否正在播放
    bool is_moving;                          // 是否正在移动
    bool has_error;                          // 是否有错误
    uint8_t error_code;                      // 错误代码
    boundary_calibration_status_t boundary_status; // 边界标定状态
    uint16_t recording_point_count;          // 录制点数
} arm_status_t;

// 电机位置数据（用于录制）
typedef struct {
    float positions[ARM_MOTOR_COUNT];        // 6个电机的位置
} arm_position_t;

// 电机边界数据
typedef struct {
    float min_positions[ARM_MOTOR_COUNT];    // 最小位置
    float max_positions[ARM_MOTOR_COUNT];    // 最大位置
    bool is_calibrated;                      // 是否已标定
} arm_boundary_t;

class DeepArm {
private:
    DeepMotor* deep_motor_;                  // 深度电机管理器
    Settings* settings_;                     // 设置存储
    
    // 机械臂状态
    arm_status_t arm_status_;
    
    // 录制数据
    arm_position_t recording_positions_[MAX_RECORDING_POINTS];
    uint16_t current_recording_index_;
    uint32_t recording_start_time_;     // 录制开始时间(ms)
    uint8_t collected_motors_;          // 当前录制点已收集的电机位掩码
    uint32_t recording_query_count_;    // 录制期间发送的查询指令数
    uint32_t recording_stop_time_;      // 录制停止时间(ms)
    
    // 边界数据
    arm_boundary_t arm_boundary_;
    
    // 最新电机位置数据（实时更新）
    float current_motor_positions_[ARM_MOTOR_COUNT];
    bool motor_position_valid_[ARM_MOTOR_COUNT];  // 位置数据是否有效
    
    // 任务句柄
    TaskHandle_t status_query_task_handle_;  // 状态查询任务句柄
    TaskHandle_t boundary_query_task_handle_; // 边界查询任务句柄
    TaskHandle_t recording_task_handle_;     // 录制任务句柄
    TaskHandle_t play_task_handle_;          // 播放任务句柄
    
    // 播放控制
    volatile bool play_stop_requested_;      // 播放停止请求标志
    
    // 电机ID数组
    uint8_t motor_ids_[ARM_MOTOR_COUNT];
    
    // 轨迹规划器
    trajectory_planner_t trajectory_planner_;        // 用于播放录制轨迹
    trajectory_planner_t move_to_first_planner_;     // 用于回到第一点的轨迹规划
    trajectory_config_t trajectory_config_;
    
    // 静态任务函数
    static void statusQueryTask(void* parameter);
    static void boundaryQueryTask(void* parameter);
    static void recordingTask(void* parameter);
    static void playTask(void* parameter);
    
    // 私有辅助函数
    bool loadBoundaryData();
    bool saveBoundaryData();
    bool checkPositionLimits(const arm_position_t& position);
    void printBoundaryData();

public:
    /**
     * @brief 构造函数
     * @param deep_motor 深度电机管理器指针
     * @param motor_ids 6个电机的ID数组
     */
    DeepArm(DeepMotor* deep_motor, const uint8_t motor_ids[ARM_MOTOR_COUNT]);
    
    /**
     * @brief 析构函数
     */
    ~DeepArm();
    
    /**
     * @brief 设置零位 - 同时设置6个电机的零位
     * @return true 成功, false 失败
     */
    bool setZeroPosition();
    
    /**
     * @brief 启动机械臂 - 同时使能6个电机
     * @return true 成功, false 失败
     */
    bool enableArm();
    
    /**
     * @brief 关闭机械臂 - 同时关闭6个电机
     * @return true 成功, false 失败
     */
    bool disableArm();
    
    /**
     * @brief 初始化机械臂
     * @param max_speeds 6个电机的最大速度数组，默认30.0 rad/s
     * @return true 成功, false 失败
     */
    bool initializeArm(const float max_speeds[ARM_MOTOR_COUNT] = nullptr);
    
    /**
     * @brief 开始录制 - 同时录制6个电机的动作
     * @return true 成功, false 失败
     */
    bool startRecording();
    
    /**
     * @brief 停止录制
     * @return true 成功, false 失败
     */
    bool stopRecording();
    
    /**
     * @brief 播放录制 - 同时播放6个电机的动作
     * @param loop_count 循环次数，0表示无限循环，1表示播放一次
     * @return true 成功, false 失败
     */
    bool playRecording(uint32_t loop_count = 1);
    
    /**
     * @brief 停止播放录制
     * @return true 成功, false 失败
     */
    bool stopPlayback();
    
    /**
     * @brief 检查是否正在播放
     * @return true 播放中, false 未播放
     */
    bool isPlaying() const;
    
    /**
     * @brief 从当前位置平滑移动到目标位置
     * @param target_positions 目标位置数组
     * @return true 成功, false 失败
     */
    bool moveToPosition(const float target_positions[ARM_MOTOR_COUNT]);
    
    /**
     * @brief 从最后一点平滑移动到第一点（播放前准备）
     * @return true 成功, false 失败
     */
    bool moveToFirstPoint();
    
    /**
     * @brief 启动状态查询任务
     * @return true 成功, false 失败
     */
    bool startStatusQueryTask();
    
    /**
     * @brief 停止状态查询任务
     * @return true 成功, false 失败
     */
    bool stopStatusQueryTask();
    
    /**
     * @brief 开始边界位置标定
     * @return true 成功, false 失败
     */
    bool startBoundaryCalibration();
    
    /**
     * @brief 停止边界位置标定
     * @return true 成功, false 失败
     */
    bool stopBoundaryCalibration();
    
    /**
     * @brief 设置电机位置（带边界检查）
     * @param positions 6个电机的位置数组
     * @param max_speeds 6个电机的最大速度数组，可选
     * @return true 成功, false 失败
     */
    bool setArmPosition(const float positions[ARM_MOTOR_COUNT], 
                       const float max_speeds[ARM_MOTOR_COUNT] = nullptr);
    
    /**
     * @brief 获取机械臂状态
     * @param status 状态结构体指针
     * @return true 成功, false 失败
     */
    bool getArmStatus(arm_status_t* status) const;
    
    /**
     * @brief 获取边界数据
     * @param boundary 边界数据结构体指针
     * @return true 成功, false 失败
     */
    bool getArmBoundary(arm_boundary_t* boundary) const;
    
    /**
     * @brief 获取最新电机位置数据
     * @param positions 6个电机的位置数组
     * @return true 成功, false 失败（如果数据无效）
     */
    bool getCurrentMotorPositions(float positions[ARM_MOTOR_COUNT]) const;
    
    /**
     * @brief 检查所有电机是否已启动
     * @return true 所有电机已启动, false 有电机未启动
     */
    bool areAllMotorsEnabled() const;
    
    /**
     * @brief 检查电机是否在安全状态下可以初始化
     * @return true 可以安全初始化, false 不安全
     */
    bool canSafelyInitialize() const;
    
    /**
     * @brief 执行单次播放（内部方法）
     * @return true 成功, false 失败
     */
    bool executeSinglePlay();
    
    /**
     * @brief 检查是否正在录制
     * @return true 录制中, false 非录制状态
     */
    bool isRecording() const;
    
    /**
     * @brief 检查录制数据是否就绪
     * @return true 数据就绪, false 数据未就绪
     */
    bool isRecordingDataReady() const;
    
    /**
     * @brief 获取录制点数
     * @return 录制点数
     */
    uint16_t getRecordingPointCount() const;
    
    /**
     * @brief 检查边界是否已标定
     * @return true 已标定, false 未标定
     */
    bool isBoundaryCalibrated() const;
    
    /**
     * @brief 打印机械臂状态信息（调试用）
     */
    void printArmStatus() const;
};

#endif // _DEEP_ARM_H__
