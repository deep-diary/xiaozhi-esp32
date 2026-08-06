#ifndef _DEEP_DOG_MOTOR_CONFIG_H_
#define _DEEP_DOG_MOTOR_CONFIG_H_

/**
 * 电机/协议/保护相关配置（从 board `config.h` 拆分）。
 *
 * 目标：
 * - 复用 `motor/` 目录时，不必依赖上层板级 `config.h`
 * - 保持原有宏名不变，减少改动面
 */

/**
 * CAN 报文日志（motor 模块默认值；若板级 config.h 已定义同名宏会覆盖）。
 * 兼容旧宏 DEEP_DOG_CAN_HEX_LOG：仅在新宏未定义时兜底。
 */
#ifndef DEEP_DOG_CAN_RX_HEX_LOG
#ifdef DEEP_DOG_CAN_HEX_LOG
#define DEEP_DOG_CAN_RX_HEX_LOG DEEP_DOG_CAN_HEX_LOG
#else
#define DEEP_DOG_CAN_RX_HEX_LOG 0
#endif
#endif
#ifndef DEEP_DOG_CAN_TX_HEX_LOG
#ifdef DEEP_DOG_CAN_HEX_LOG
#define DEEP_DOG_CAN_TX_HEX_LOG DEEP_DOG_CAN_HEX_LOG
#else
#define DEEP_DOG_CAN_TX_HEX_LOG 0
#endif
#endif

/** 运控（MIT）整步验证：1=关节下发改用运控 CAN 帧 */
#ifndef DEEP_DOG_USE_MIT_WALK
#define DEEP_DOG_USE_MIT_WALK 1
#endif

/** 1=仅初始化/失能/下发左前腿(FL)，其余腿跳过（台架单腿验证） */
#ifndef DEEP_DOG_MIT_VALIDATE_FL_ONLY
#define DEEP_DOG_MIT_VALIDATE_FL_ONLY 0
#endif

/** 运控默认增益与前馈（腿/整机共用） */
#ifndef DEEP_DOG_MIT_DEFAULT_KP
#define DEEP_DOG_MIT_DEFAULT_KP 20.0f
#endif
#ifndef DEEP_DOG_MIT_DEFAULT_KD
#define DEEP_DOG_MIT_DEFAULT_KD 1.8f
#endif
#ifndef DEEP_DOG_MIT_DEFAULT_TAU_FF
#define DEEP_DOG_MIT_DEFAULT_TAU_FF 0.0f
#endif

/** MIT 下 v_des（rad/s） */
#ifndef DEEP_DOG_MIT_VDES_RAD_S
#define DEEP_DOG_MIT_VDES_RAD_S 0.0f
#endif

/**
 * 初始化阶段（刚使能）用于“上电保持帧”的更保守增益。
 * - 目的：降低误动作/零位异常时对结构的冲击；与行走/姿态控制的全局增益解耦。
 */
#ifndef DEEP_DOG_MIT_INIT_HOLD_KP
#define DEEP_DOG_MIT_INIT_HOLD_KP (DEEP_DOG_MIT_DEFAULT_KP * 0.1f)  // kp/10
#endif
#ifndef DEEP_DOG_MIT_INIT_HOLD_KD
#define DEEP_DOG_MIT_INIT_HOLD_KD (DEEP_DOG_MIT_DEFAULT_KD * 2.0f)   // kd*2
#endif

/**
 * 扭矩保护（碰撞/堵转）：
 * - 台架测最大扭矩阶段：将 `DEEP_DOG_TORQUE_LIMIT_NM` 设为 0，仅记录并打印观测到的最大 |扭矩|
 * - 确认后：把 `DEEP_DOG_TORQUE_LIMIT_NM` 设为你的上限（N·m），超过即置 collision 标志并在发送侧停扭保护
 */
#ifndef DEEP_DOG_TORQUE_LIMIT_NM
#define DEEP_DOG_TORQUE_LIMIT_NM 0.0f
#endif
/** 只有当最大 |扭矩| 增量超过该值才打印一次（避免刷屏） */
#ifndef DEEP_DOG_TORQUE_MAX_LOG_DELTA_NM
#define DEEP_DOG_TORQUE_MAX_LOG_DELTA_NM 0.1f
#endif

/**
 * 电机「速度档位」占位宏（0～100 整数比例）。
 * 当前工程内运控多用 rad/s；若语音/UI 用百分比速度可在此换算。
 */
#ifndef MOTOR_SPEED_MAX
#define MOTOR_SPEED_MAX 100
#endif
#ifndef MOTOR_SPEED_80
#define MOTOR_SPEED_80  80
#endif
#ifndef MOTOR_SPEED_60
#define MOTOR_SPEED_60  60
#endif
#ifndef MOTOR_SPEED_MIN
#define MOTOR_SPEED_MIN 0
#endif

/**
 * 异步初始化（旧固件无主动上报时推荐）：
 * 1=发指令后不阻塞等零位，RX 环判定 ready；0=同步阻塞校验（旧行为）
 */
#ifndef DEEP_DOG_MOTOR_INIT_ASYNC
#define DEEP_DOG_MOTOR_INIT_ASYNC 1
#endif

#ifndef DEEP_DOG_MOTOR_INIT_ZERO_TOL_RAD
#define DEEP_DOG_MOTOR_INIT_ZERO_TOL_RAD 0.15f
#endif

#ifndef DEEP_DOG_MOTOR_INIT_TIMEOUT_MS
#define DEEP_DOG_MOTOR_INIT_TIMEOUT_MS 30000
#endif

#endif  // _DEEP_DOG_MOTOR_CONFIG_H_
