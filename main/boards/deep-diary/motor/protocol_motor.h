#ifndef _PROTOCOL_MOTOR_H__
#define _PROTOCOL_MOTOR_H__

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include "../can/ESP32-TWAI-CAN.hpp"

/**
 * @brief 电机协议层 - 用于构建和发送CAN帧控制小米电机
 * 
 * 支持多电机控制，每个电机通过唯一的ID进行标识
 * 提供统一的接口来构建各种控制命令的CAN帧
 */

// CAN配置常量
#define MOTOR_MASTER_ID       0xFD
#define MOTOR_CAN_TIMEOUT_MS  10

// 数学常量
#ifndef MOTOR_PI
#define MOTOR_PI 3.14159265359f
#endif

// 常量定义
#define P_MIN           -12.5f 
#define P_MAX           12.5f 
#define V_MIN           -30.0f 
#define V_MAX           30.0f 
#define KP_MIN          0.0f 
#define KP_MAX          500.0f 
#define KD_MIN          0.0f 
#define KD_MAX          5.0f 
#define T_MIN           -12.0f 
#define T_MAX           12.0f
#define I_MIN           -23.0f
#define I_MAX           23.0f

// 转换系数
#define INT2ANGLE       0.000383501049f  // angle = data * 8PI/65535 - 4PI
#define INT2SPEED       0.000915541314f  // speed = data * 60/65535 - 30
#define INT2TORQUE      0.000366216526f  // torque = data * 24/65535 - 12

// 宏定义用于解析29位ID
// 29位CAN ID布局：
// bit 0-7:   Master ID (8 bits)
// bit 8-15:  Motor ID (8 bits)
// bit 16-21: Error flags (6 bits)
// bit 22-23: Mode status (2 bits)
// bit 24-28: Command type (5 bits)
#define RX_29ID_DISASSEMBLE_MASTER_ID(id)       (uint8_t)((id)&0xFF)
#define RX_29ID_DISASSEMBLE_MOTOR_ID(id)        (uint8_t)(((id)>>8)&0xFF)
#define RX_29ID_DISASSEMBLE_ERR_STA(id)         (uint8_t)(((((id)>>16)&0x3F) > 0) ? 1 : 0)
#define RX_29ID_DISASSEMBLE_HALL_ERR(id)        (uint8_t)(((id)>>20)&0X01)
#define RX_29ID_DISASSEMBLE_MAGNET_ERR(id)      (uint8_t)(((id)>>19)&0x01)
#define RX_29ID_DISASSEMBLE_TEMP_ERR(id)        (uint8_t)(((id)>>18)&0x01)
#define RX_29ID_DISASSEMBLE_CURRENT_ERR(id)     (uint8_t)(((id)>>17)&0x01)
#define RX_29ID_DISASSEMBLE_VOLTAGE_ERR(id)     (uint8_t)(((id)>>16)&0x01)
#define RX_29ID_DISASSEMBLE_MODE_STA(id)        (uint8_t)(((id)>>22)&0x03)
#define RX_29ID_DISASSEMBLE_CMD_TYPE(id)        (uint8_t)(((id)>>24)&0x1F)  // 只取5位，不是8位

// 宏定义用于解析数据
#define RX_DATA_DISASSEMBLE_CUR_ANGLE(data)     (uint16_t)((data[0]<<8)|data[1])
#define RX_DATA_DISASSEMBLE_CUR_SPEED(data)     (uint16_t)((data[2]<<8)|data[3])
#define RX_DATA_DISASSEMBLE_CUR_TORQUE(data)    (uint16_t)((data[4]<<8)|data[5])
#define RX_DATA_DISASSEMBLE_CUR_TEMP(data)      (uint16_t)((data[6]<<8)|data[7])

// 电机控制模式
typedef enum {
    MOTOR_MODE_RESET = 0,      // 复位模式
    MOTOR_MODE_CALIBRATE = 1,  // 标定模式  
    MOTOR_MODE_RUN = 2         // 运行模式
} motor_mode_t;

// 电机运行模式（参考小米电机协议）
typedef enum {
    MOTOR_CTRL_MODE = 0,       // 运控模式
    MOTOR_POS_MODE = 1,        // 位置模式
    MOTOR_SPEED_MODE = 2,      // 速度模式
    MOTOR_CURRENT_MODE = 3     // 电流模式
} motor_run_mode_t;

// 电机控制命令类型
typedef enum {
    MOTOR_CMD_FEEDBACK = 0x02,   // 反馈帧
    MOTOR_CMD_ENABLE = 3,      // 使能
    MOTOR_CMD_RESET = 4,       // 停止
    MOTOR_CMD_SET_ZERO = 6,    // 设置零点
    MOTOR_CMD_CONTROL = 1,     // 运控模式
    MOTOR_CMD_SET_PARAM = 18,  // 设置参数
    MOTOR_CMD_VERSION = 0x17   // 获取软件版本号
} motor_cmd_t;

// 参数类型定义
typedef enum {
    PARAM_SIN_ENABLE = 0x7001,    // 正弦幅值
    PARAM_SIN_FREQ = 0x7002,   // 正弦频率
    PARAM_SIN_AMP = 0x7003,  // 正弦使能
    PARAM_RUN_MODE = 0x7005,   // 运行模式
    PARAM_IQ_REF = 0x7006,     // 电流指令
    PARAM_SPD_REF = 0x700A,    // 速度指令
    PARAM_LIMIT_TORQUE = 0x700B, // 转矩限制
    PARAM_LOC_REF = 0x7016,    // 位置指令
    PARAM_LIMIT_SPD = 0x7017,  // 速度限制
    PARAM_LIMIT_CUR = 0x7018,  // 电流限制
    PARAM_LOC_KP = 0x701E,     // 位置Kp
    PARAM_SPD_KP = 0x701F,     // 速度Kp
    PARAM_SPD_KI = 0x7020      // 速度Ki
} motor_param_t;

// 电机状态结构体
typedef struct {
    uint8_t master_id;
    uint8_t motor_id;
    uint8_t error_status;
    uint8_t hall_error;
    uint8_t magnet_error;
    uint8_t temp_error;
    uint8_t current_error;
    uint8_t voltage_error;
    motor_mode_t mode_status;  // 电机模式状态（使用枚举类型）
    float current_angle;       // 当前角度 (弧度)
    float current_speed;       // 当前速度 (rad/s)
    float current_torque;      // 当前扭矩 (N·m)
    float current_temp;        // 当前温度 (°C)
    char version[9];          // 软件版本号
} motor_status_t;

class MotorProtocol {
public:

    /**
     * @brief 电机使能
     * @param motor_id 电机ID
     * @return true 成功, false 失败
     */
    static bool enableMotor(uint8_t motor_id);

    /**
     * @brief 电机停止
     * @param motor_id 电机ID
     * @return true 成功, false 失败
     */
    static bool resetMotor(uint8_t motor_id);

    /**
     * @brief 设置电机机械零点
     * @param motor_id 电机ID
     * @return true 成功, false 失败
     */
    static bool setMotorZero(uint8_t motor_id);

    /**
     * @brief 运控模式控制
     * @param motor_id 电机ID
     * @param torque 扭矩 (-12.0 ~ +12.0 N·m)
     * @param position 位置 (-12.5 ~ +12.5 rad)
     * @param speed 速度 (-30.0 ~ +30.0 rad/s)
     * @param kp 位置Kp (0.0 ~ 500.0)
     * @param kd 位置Kd (0.0 ~ 5.0)
     * @return true 成功, false 失败
     */
    static bool controlMotor(uint8_t motor_id, float torque, float position, 
                           float speed, float kp, float kd);

    /**
     * @brief 设置电机位置
     * @param motor_id 电机ID
     * @param position 目标位置 (-12.5 ~ +12.5 rad)
     * @param max_speed 最大速度 (0.0 ~ 30.0 rad/s)
     * @return true 成功, false 失败
     */
    static bool setPosition(uint8_t motor_id, float position, float max_speed);

    /**
     * @brief 只设置电机位置（不设置速度限制）
     * @param motor_id 电机ID
     * @param position 目标位置 (-12.5 ~ +12.5 rad)
     * @return true 成功, false 失败
     */
    static bool setPositionOnly(uint8_t motor_id, float position);

    /**
     * @brief 设置电机速度
     * @param motor_id 电机ID
     * @param speed 目标速度 (-30.0 ~ +30.0 rad/s)
     * @return true 成功, false 失败
     */
    static bool setSpeed(uint8_t motor_id, float speed);

    /**
     * @brief 设置电机电流
     * @param motor_id 电机ID
     * @param current 目标电流 (-23.0 ~ +23.0 A)
     * @return true 成功, false 失败
     */
    static bool setCurrent(uint8_t motor_id, float current);

    /**
     * @brief 切换电机运行模式
     * @param motor_id 电机ID
     * @param mode 运行模式
     * @return true 成功, false 失败
     */
    static bool changeMotorMode(uint8_t motor_id, motor_run_mode_t mode);

    /**
     * @brief 设置电机运行模式（参考小米电机协议）
     * @param motor_id 电机ID
     * @param mode 运行模式：CTRL_MODE(0) 电流模式：CUR_MODE(1) 速度模式：SPEED_MODE(2) 位置模式：POS_MODE(3)
     * @return true 成功, false 失败
     */
    static bool setMotorRunMode(uint8_t motor_id, uint8_t mode);

    /**
     * @brief 设置电机为运控模式
     * @param motor_id 电机ID
     * @return true 成功, false 失败
     */
    static bool setMotorControlMode(uint8_t motor_id);

    /**
     * @brief 设置电机为电流模式
     * @param motor_id 电机ID
     * @return true 成功, false 失败
     */
    static bool setMotorCurrentMode(uint8_t motor_id);

    /**
     * @brief 设置电机为速度模式
     * @param motor_id 电机ID
     * @return true 成功, false 失败
     */
    static bool setMotorSpeedMode(uint8_t motor_id);

    /**
     * @brief 设置电机为位置模式
     * @param motor_id 电机ID
     * @return true 成功, false 失败
     */
    static bool setMotorPositionMode(uint8_t motor_id);

    /**
     * @brief 电机初始化函数
     * @param motor_id 电机ID
     * @param max_speed 最大速度 (rad/s)，默认30.0
     * @return true 成功, false 失败
     * 
     * 初始化流程：
     * 1. 将电机切换到位置模式
     * 2. 设置最大速度限制
     * 3. 使能电机
     * 4. 启动1秒定时状态查询任务
     */
    static bool initializeMotor(uint8_t motor_id, float max_speed = 30.0f);

    /**
     * @brief 设置电机参数
     * @param motor_id 电机ID
     * @param param 参数类型
     * @param value 参数值
     * @return true 成功, false 失败
     */
    static bool setMotorParameter(uint8_t motor_id, motor_param_t param, float value);

    /**
     * @brief 设置电机参数（原始字节数据）
     * @param motor_id 电机ID
     * @param param 参数类型
     * @param data_bytes 4字节参数数据数组
     * @return true 成功, false 失败
     */
    static bool setMotorParameterRaw(uint8_t motor_id, motor_param_t param, const uint8_t data_bytes[4]);


    /**
     * @brief 解析电机返回数据
     * @param can_frame CAN帧
     * @param status 状态结构体指针
     */
    static void parseMotorData(const CanFrame& can_frame, motor_status_t* status);

    /**
     * @brief 开始sin 正弦信号
     * @param motor_id 电机ID
     * @param amp 幅度
     * @param freq 频率
     */
    static bool startSinSignal(uint8_t motor_id, float amp, float freq);

    /**
     * @brief 停止sin 正弦信号
     * @param motor_id 电机ID
     */
    static bool stopSinSignal(uint8_t motor_id);

private:
    /**
     * @brief 构建CAN帧ID
     * @param motor_id 电机ID
     * @param cmd 命令类型
     * @return 29位CAN ID
     */
    static uint32_t buildCanId(uint8_t motor_id, motor_cmd_t cmd);

    /**
     * @brief 发送CAN帧
     * @param frame CAN帧
     * @return true 成功, false 失败
     */
    static bool sendCanFrame(const CanFrame& frame);

    /**
     * @brief 浮点数转换为16位整数
     * @param value 浮点值
     * @param min_val 最小值
     * @param max_val 最大值
     * @param bits 位数
     * @return 转换后的整数
     */
    static uint16_t floatToUint16(float value, float min_val, float max_val, int bits);
};

#endif // _PROTOCOL_MOTOR_H__
