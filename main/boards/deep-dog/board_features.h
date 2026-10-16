#ifndef _DEEP_DOG_BOARD_FEATURES_H_
#define _DEEP_DOG_BOARD_FEATURES_H_

/**
 * 产品功能组合（同硬件只改本文件 + config.h 的 EXT_PIN_MODE）。
 * 须在 config.h 定义 *_AVAILABLE 之后 include。
 * 剖面表见 FEATURE_FLAGS.md。
 *
 * 38/48 成对引出脚同一时刻只有一种 EXT_PIN_MODE（config.h），互斥。
 * *_ENABLE 只表示是否编入对应软件栈；无 *_AVAILABLE 时由下方钳位强制为 0。
 *
 * 依赖链：
 *   CAN_AVAILABLE → CAN_ENABLE → MOTOR_ENABLE → DOG | ARM
 *   PWM_AVAILABLE → SERVO | GIMBAL
 *   IO_AVAILABLE  → IO（纯 GPIO，与 LED 模式互斥）
 *   LED_AVAILABLE → LED（WS2812；DIN 默认 gpio_a，可在 config.h 覆盖 LED_STRIP_GPIO）
 */

/* -------- 联调剖面（首选开关：只改下面这一行）--------
 * 见 docs/requirements/config/CFG-03-compile-time-profiles.md。
 * 剖面只做「预设赋值」：下方 *_ENABLE 的默认值与依赖钳位逻辑保持原样，
 * 已被剖面显式定义的宏不会被后面的 #ifndef 覆盖；仍可用 -DDEEP_DOG_XXX_ENABLE=n 单独覆盖。
 * 只管 CAN 产品链（CAN/MOTOR/DOG/ARM）；UART/RS485/IO/AD/LED/SERVO/GIMBAL 由 CFG-02 运行时模式决定。 */
#define DEEP_DOG_PROFILE_CAN_COMM 1  /** CAN 透传联调：CAN 1 / MOTOR 0 / DOG 0 / ARM 0（不背电机栈） */
#define DEEP_DOG_PROFILE_MOTOR    2  /** 单电机调试：CAN 1 / MOTOR 1 / DOG 0 / ARM 0 */
#define DEEP_DOG_PROFILE_DOG      3  /** 四足运控：  CAN 1 / MOTOR 1 / DOG 1 / ARM 0 */
#define DEEP_DOG_PROFILE_FULL     4  /** 全功能：    不改 CAN 产品链，沿用下方默认值 */

#ifndef DEEP_DOG_PROFILE
#define DEEP_DOG_PROFILE DEEP_DOG_PROFILE_CAN_COMM
#endif

#if DEEP_DOG_PROFILE == DEEP_DOG_PROFILE_CAN_COMM
#define DEEP_DOG_MOTOR_ENABLE 0
#elif DEEP_DOG_PROFILE == DEEP_DOG_PROFILE_MOTOR
#define DEEP_DOG_MOTOR_ENABLE 1
#elif DEEP_DOG_PROFILE == DEEP_DOG_PROFILE_DOG
#define DEEP_DOG_MOTOR_ENABLE 1
#define DEEP_DOG_DOG_ENABLE 1
#endif

/* -------- 剖面隐含的编译期兜底引出脚模式（随剖面一起选，勿单独改）--------
 * 目的：免去「裁掉 CAN 产品链却忘了改 config.h 默认模式」的手工联动
 *       —— 否则首启 ResolveBootTarget() 取到不支持的 can，apply 失败回滚 none。
 * 注意：本文件由 config.h 在其 `DEEP_DOG_EXT_PIN_MODE` / `*_MODE_STR` 之后 include，
 *       故这里必须先 #undef 再定义（含 STR，否则宏重定义告警 + 日志 default= 印错）。
 * PROFILE_FULL = 不干预，沿用 config.h 的值（要手工指定模式时选它）。 */
#if defined(DEEP_DOG_EXT_PIN_MODE) && (DEEP_DOG_PROFILE == DEEP_DOG_PROFILE_CAN_COMM || \
                                       DEEP_DOG_PROFILE == DEEP_DOG_PROFILE_MOTOR ||     \
                                       DEEP_DOG_PROFILE == DEEP_DOG_PROFILE_DOG)
#undef DEEP_DOG_EXT_PIN_MODE
#undef DEEP_DOG_EXT_PIN_MODE_STR
#define DEEP_DOG_EXT_PIN_MODE DEEP_DOG_EXT_PIN_CAN
#define DEEP_DOG_EXT_PIN_MODE_STR "can"
#endif

/* -------- 引出脚外设（运行时模式选择，见 CFG-02；A/B 默认 38/48） --------
 * 全部默认编入 = 1：由 ext_pin/ 在启动时按生效模式只初始化其中一个。 */
#ifndef DEEP_DOG_CAN_ENABLE
#define DEEP_DOG_CAN_ENABLE 1
#endif
#ifndef DEEP_DOG_MOTOR_ENABLE
#define DEEP_DOG_MOTOR_ENABLE 1
#endif
#ifndef DEEP_DOG_UART_ENABLE
#define DEEP_DOG_UART_ENABLE 1
#endif
#ifndef DEEP_DOG_RS485_ENABLE
#define DEEP_DOG_RS485_ENABLE 1
#endif
/** EXT_PIN=IO：两路普通 GPIO 输入/输出（非 WS2812；灯带用 LED_ENABLE） */
#ifndef DEEP_DOG_IO_ENABLE
#define DEEP_DOG_IO_ENABLE 1
#endif
#ifndef DEEP_DOG_AD_ENABLE
#define DEEP_DOG_AD_ENABLE 1
#endif
/** EXT_PIN=LED：WS2812 灯带（DIN 默认 gpio_a；脚位见 config.h DEEP_DOG_LED_STRIP_GPIO） */
#ifndef DEEP_DOG_LED_ENABLE
#define DEEP_DOG_LED_ENABLE 1
#endif

/* -------- CAN 产品链（须 CAN+MOTOR）--------
 * 产品级开关（非引脚模式）：保持编译期决定，运行时模式只决定 CAN 硬件是否可用。 */
#ifndef DEEP_DOG_DOG_ENABLE
#define DEEP_DOG_DOG_ENABLE 0
#endif
#ifndef DEEP_DOG_ARM_ENABLE
#define DEEP_DOG_ARM_ENABLE 0
#endif

/* -------- 引出脚 PWM 产品（生效模式=PWM 时才初始化） -------- */
#ifndef DEEP_DOG_SERVO_ENABLE
#define DEEP_DOG_SERVO_ENABLE 1
#endif
#ifndef DEEP_DOG_GIMBAL_ENABLE
#define DEEP_DOG_GIMBAL_ENABLE 1
#endif

/* -------- 与 38/48 引出脚无关 -------- */
#ifndef DEEP_DOG_MQTT_ENABLE
#define DEEP_DOG_MQTT_ENABLE 1
#endif
#ifndef DEEP_DOG_TRACK_MQTT_ENABLE
#define DEEP_DOG_TRACK_MQTT_ENABLE 1
#endif
#ifndef DEEP_DOG_VISION_HUB_ENABLE
#define DEEP_DOG_VISION_HUB_ENABLE 1
#endif
#ifndef DEEP_DOG_FACE_AI_ENABLE
#define DEEP_DOG_FACE_AI_ENABLE 1
#endif
#ifndef DEEP_DOG_IMU_ENABLE
#define DEEP_DOG_IMU_ENABLE 1
#endif
/* 注意：独立 HTTP server 与 WS MCP 并存会耗尽 internal 内存（实测 free_int≈12KB、
 * largest≈3.8KB，两者 httpd_start 均失败：ESP_ERR_HTTPD_TASK）。
 * 故默认关闭；数据点 REST 端点复用 WS MCP 的 httpd（见 deep_dog_ws_mcp_server）。 */
#ifndef DEEP_DOG_HTTP_SERVER_ENABLE
#define DEEP_DOG_HTTP_SERVER_ENABLE 0
#endif
#ifndef DEEP_DOG_HANDLE_ENABLE
#define DEEP_DOG_HANDLE_ENABLE 1
#endif

/* -------- 依赖钳位（*_AVAILABLE 必须已由 config.h 定义；未定义时不钳位） -------- */
#if defined(DEEP_DOG_CAN_AVAILABLE) && !DEEP_DOG_CAN_AVAILABLE
#undef DEEP_DOG_CAN_ENABLE
#define DEEP_DOG_CAN_ENABLE 0
#endif
#if !DEEP_DOG_CAN_ENABLE
#undef DEEP_DOG_MOTOR_ENABLE
#define DEEP_DOG_MOTOR_ENABLE 0
#endif
#if !DEEP_DOG_MOTOR_ENABLE
#undef DEEP_DOG_DOG_ENABLE
#define DEEP_DOG_DOG_ENABLE 0
#undef DEEP_DOG_ARM_ENABLE
#define DEEP_DOG_ARM_ENABLE 0
#endif

#if defined(DEEP_DOG_UART_AVAILABLE) && !DEEP_DOG_UART_AVAILABLE
#undef DEEP_DOG_UART_ENABLE
#define DEEP_DOG_UART_ENABLE 0
#endif
#if defined(DEEP_DOG_RS485_AVAILABLE) && !DEEP_DOG_RS485_AVAILABLE
#undef DEEP_DOG_RS485_ENABLE
#define DEEP_DOG_RS485_ENABLE 0
#endif
#if defined(DEEP_DOG_IO_AVAILABLE) && !DEEP_DOG_IO_AVAILABLE
#undef DEEP_DOG_IO_ENABLE
#define DEEP_DOG_IO_ENABLE 0
#endif
#if defined(DEEP_DOG_AD_AVAILABLE) && !DEEP_DOG_AD_AVAILABLE
#undef DEEP_DOG_AD_ENABLE
#define DEEP_DOG_AD_ENABLE 0
#endif
#if defined(DEEP_DOG_LED_AVAILABLE) && !DEEP_DOG_LED_AVAILABLE
#undef DEEP_DOG_LED_ENABLE
#define DEEP_DOG_LED_ENABLE 0
#endif

#if defined(DEEP_DOG_PWM_AVAILABLE) && !DEEP_DOG_PWM_AVAILABLE
#undef DEEP_DOG_SERVO_ENABLE
#define DEEP_DOG_SERVO_ENABLE 0
#undef DEEP_DOG_GIMBAL_ENABLE
#define DEEP_DOG_GIMBAL_ENABLE 0
#endif
/* 同脚互斥：同时开时优先云台；底层 Servo 驱动随 GIMBAL|SERVO 任一开启 */
#if DEEP_DOG_GIMBAL_ENABLE && DEEP_DOG_SERVO_ENABLE
#undef DEEP_DOG_SERVO_ENABLE
#define DEEP_DOG_SERVO_ENABLE 0
#endif

#endif  // _DEEP_DOG_BOARD_FEATURES_H_
