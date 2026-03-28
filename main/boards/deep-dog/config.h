#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/uart.h>

#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 16000

#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_45
#define AUDIO_I2S_GPIO_WS GPIO_NUM_41
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_39
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_40
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_42

#define AUDIO_CODEC_PA_PIN       GPIO_NUM_46
#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_4
#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_5
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR

#define BUILTIN_LED_GPIO        GPIO_NUM_NC
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_NC
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_NC

#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  240
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false

#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0

#define DISPLAY_DC_GPIO     GPIO_NUM_43
#define DISPLAY_CS_GPIO     GPIO_NUM_44
#define DISPLAY_CLK_GPIO    GPIO_NUM_21
#define DISPLAY_MOSI_GPIO   GPIO_NUM_47
#define DISPLAY_RST_GPIO    GPIO_NUM_NC

#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_46
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false

// #define UART_ECHO_TXD GPIO_NUM_38
// #define UART_ECHO_RXD GPIO_NUM_48
#define UART_ECHO_RTS (-1)
#define UART_ECHO_CTS (-1)

// CAN总线 (TWAI)：ESP32-S3 通过 GPIO 矩阵连接 TWAI，TX/RX 可任意选可用 GPIO。
// GPIO 38、48 为普通 I/O，非 Strapping，可作为 CAN TX/RX 使用。
// 接线：ESP32 TX(38) -> 收发器 RXD；ESP32 RX(48) <- 收发器 TXD；共地；总线两端 120Ω 终端；波特率与从站一致（当前 1Mbps）。
#define CAN_TX_GPIO GPIO_NUM_38   // TX -> 收发器 RXD
#define CAN_RX_GPIO GPIO_NUM_48   // RX <- 收发器 TXD

/**
 * 电机「速度档位」占位宏（0～100 整数比例），与 esp-sparkbot 等板级模板同源。
 * 当前 deep-dog 工程内 **无任何 .cc/.h 引用**（电机实际限速用 rad/s，见 protocol_motor / MCP / LegControl）。
 * 若后续做语音「百分之八十速度」、UI 滑条或步态占空比，可在此统一换算；否则可删除或保持不动。
 */
#define MOTOR_SPEED_MAX 100
#define MOTOR_SPEED_80  80
#define MOTOR_SPEED_60  60
#define MOTOR_SPEED_MIN 0

/** 1=UART 打印 CAN 收发帧 id+dlc+8 字节 HEX（核对指令用）；调完改 0 减轻日志与串口负载 */
#ifndef DEEP_DOG_CAN_HEX_LOG
#define DEEP_DOG_CAN_HEX_LOG 0
#endif

/** 运控（MIT）整步验证：1=关节下发改用运控 CAN 帧（见 protocol_motor::controlMotor） */
#ifndef DEEP_DOG_USE_MIT_WALK
#define DEEP_DOG_USE_MIT_WALK 1
#endif
/** 1=仅初始化/失能/下发左前腿(FL)，其余腿跳过（台架单腿验证） */
#ifndef DEEP_DOG_MIT_VALIDATE_FL_ONLY
#define DEEP_DOG_MIT_VALIDATE_FL_ONLY 0
#endif

/** 运控默认增益与前馈（腿/整机共用，与单电机 MCP 默认 1/1/0 对齐）；仅当 DEEP_DOG_USE_MIT_WALK=1 时参与编译下发 */
#ifndef DEEP_DOG_MIT_DEFAULT_KP
#define DEEP_DOG_MIT_DEFAULT_KP 10.0f
#endif
#ifndef DEEP_DOG_MIT_DEFAULT_KD
#define DEEP_DOG_MIT_DEFAULT_KD 1.5f
#endif
#ifndef DEEP_DOG_MIT_DEFAULT_TAU_FF
#define DEEP_DOG_MIT_DEFAULT_TAU_FF 0.0f
#endif
/** LegControl::init 中 MIT 模式 initializeMotorMitMode 的 PARAM_LIMIT_SPD（rad/s） */
#ifndef DEEP_DOG_MIT_INIT_SPEED_LIMIT_RAD_S
#define DEEP_DOG_MIT_INIT_SPEED_LIMIT_RAD_S 0.2f
#endif

/** 步态一个正弦周期的采样点数（越大越平滑、步频任务更密） */
#ifndef DEEP_DOG_GAIT_TOTAL_STEPS
#define DEEP_DOG_GAIT_TOTAL_STEPS 50
#endif

/**
 * 周期优先配置（推荐）：
 * - 以「完整步态周期（一个正弦周期）」时长定义快慢范围；
 * - 每个小步间隔 step_period_ms 由 周期/采样点数 自动推导。
 * 这样后续只改 `DEEP_DOG_GAIT_TOTAL_STEPS`，step_period 范围会自动同步，无需手工改三处参数。
 */
#ifndef DEEP_DOG_BIG_STEP_PERIOD_MS_MIN
#define DEEP_DOG_BIG_STEP_PERIOD_MS_MIN 500
#endif
#ifndef DEEP_DOG_BIG_STEP_PERIOD_MS_MAX
#define DEEP_DOG_BIG_STEP_PERIOD_MS_MAX 4000
#endif
#ifndef DEEP_DOG_BIG_STEP_PERIOD_MS_DEFAULT
#define DEEP_DOG_BIG_STEP_PERIOD_MS_DEFAULT 2000
#endif

/** 兼容旧宏名：这里直接按完整步态周期使用（单位 ms） */
#define DEEP_DOG_CYCLE_PERIOD_MS_MIN (DEEP_DOG_BIG_STEP_PERIOD_MS_MIN)
#define DEEP_DOG_CYCLE_PERIOD_MS_MAX (DEEP_DOG_BIG_STEP_PERIOD_MS_MAX)
#define DEEP_DOG_CYCLE_PERIOD_MS_DEFAULT (DEEP_DOG_BIG_STEP_PERIOD_MS_DEFAULT)

/** step_period_ms = 全周期 / 采样点数（取整）；并保证最小为 1ms */
#define DEEP_DOG_STEP_PERIOD_MS_MIN \
    (((DEEP_DOG_CYCLE_PERIOD_MS_MIN / DEEP_DOG_GAIT_TOTAL_STEPS) > 0) ? \
         (DEEP_DOG_CYCLE_PERIOD_MS_MIN / DEEP_DOG_GAIT_TOTAL_STEPS) : 1)
#define DEEP_DOG_STEP_PERIOD_MS_MAX \
    (((DEEP_DOG_CYCLE_PERIOD_MS_MAX / DEEP_DOG_GAIT_TOTAL_STEPS) > 0) ? \
         (DEEP_DOG_CYCLE_PERIOD_MS_MAX / DEEP_DOG_GAIT_TOTAL_STEPS) : 1)
#define DEEP_DOG_STEP_PERIOD_MS_DEFAULT \
    (((DEEP_DOG_CYCLE_PERIOD_MS_DEFAULT / DEEP_DOG_GAIT_TOTAL_STEPS) > 0) ? \
         (DEEP_DOG_CYCLE_PERIOD_MS_DEFAULT / DEEP_DOG_GAIT_TOTAL_STEPS) : 1)

/**
 * 固定点位插值通用配置（当前用于站立/卧倒，后续左倾/右倾等固定姿态也可复用）：
 * 总时长固定，点数越多则单点延时等比例减小。
 */
#ifndef DEEP_DOG_POSE_INTERP_POINTS
#define DEEP_DOG_POSE_INTERP_POINTS 50
#endif
#ifndef DEEP_DOG_POSE_INTERP_DURATION_MS
#define DEEP_DOG_POSE_INTERP_DURATION_MS 1000
#endif

/* 兼容旧宏名（站立插值） */
#ifndef DEEP_DOG_STAND_INTERP_POINTS
#define DEEP_DOG_STAND_INTERP_POINTS DEEP_DOG_POSE_INTERP_POINTS
#endif
#ifndef DEEP_DOG_STAND_INTERP_DURATION_MS
#define DEEP_DOG_STAND_INTERP_DURATION_MS DEEP_DOG_POSE_INTERP_DURATION_MS
#endif

/**
 * 底盘 MCP `speed` 整型：÷100 = 关节 PARAM_LIMIT_SPD（rad/s），与 `DogControl::setContinuousSpeed` 上限一致。
 * 适当拉大 min~max，避免「走得快慢差不多、最快仍偏慢」。
 */
#ifndef DEEP_DOG_CHASSIS_SPEED_X100_MIN
#define DEEP_DOG_CHASSIS_SPEED_X100_MIN 20
#endif
#ifndef DEEP_DOG_CHASSIS_SPEED_X100_MAX
#define DEEP_DOG_CHASSIS_SPEED_X100_MAX 500
#endif

/**
 * 髋下垂与 kp/kd（经验值，需台架调）：MIT 下位置环 kp 过小易在重力下「塌」。
 * 可先试 **kp 2～5、kd 0.8～2**（负重髋略增 kp）；正弦行走仍要兼顾柔顺，过高易抖。
 * 当前 `DEEP_DOG_MIT_DEFAULT_KP/KD` 为全局统一下发；若需分关节增益需后续扩展 LegControl。
 */

#define ECHO_UART_PORT_NUM      UART_NUM_1
#define ECHO_UART_BAUD_RATE     (115200)
#define BUF_SIZE                (1024)

typedef enum {
    LIGHT_MODE_CHARGING_BREATH = 0,
    LIGHT_MODE_POWER_LOW,
    LIGHT_MODE_ALWAYS_ON,
    LIGHT_MODE_BLINK,
    LIGHT_MODE_WHITE_BREATH_SLOW,
    LIGHT_MODE_WHITE_BREATH_FAST,
    LIGHT_MODE_FLOWING,
    LIGHT_MODE_SHOW,
    LIGHT_MODE_SLEEP,
    LIGHT_MODE_MAX
} light_mode_t;

/* Camera：与 ESP-SparkBot 一致；SCCB 与 ES8311 共用 I2C0（SDA=4/SCL=5），SIOD/SIOC 为 NC 时由 esp_video 复用该总线。
 * 若日志出现 PID=0x0：多为摄像头未供电、无上拉、或模组 SCCB 未接到该 I2C（需单独第二总线）。 */
#define SPARKBOT_CAMERA_XCLK      (GPIO_NUM_15)
#define SPARKBOT_CAMERA_PCLK      (GPIO_NUM_13)
#define SPARKBOT_CAMERA_VSYNC     (GPIO_NUM_6)
#define SPARKBOT_CAMERA_HSYNC     (GPIO_NUM_7)
#define SPARKBOT_CAMERA_D0        (GPIO_NUM_11)
#define SPARKBOT_CAMERA_D1        (GPIO_NUM_9)
#define SPARKBOT_CAMERA_D2        (GPIO_NUM_8)
#define SPARKBOT_CAMERA_D3        (GPIO_NUM_10)
#define SPARKBOT_CAMERA_D4        (GPIO_NUM_12)
#define SPARKBOT_CAMERA_D5        (GPIO_NUM_18)
#define SPARKBOT_CAMERA_D6        (GPIO_NUM_17)
#define SPARKBOT_CAMERA_D7        (GPIO_NUM_16)

#define SPARKBOT_CAMERA_PWDN      (GPIO_NUM_NC)
#define SPARKBOT_CAMERA_RESET     (GPIO_NUM_NC)
/* OV3660 官方表 240×240 RGB565 常用 20MHz 输入；16M 时部分模组读 ID 不稳定 */
#define SPARKBOT_CAMERA_XCLK_FREQ (20000000)
#define SPARKBOT_LEDC_TIMER       (LEDC_TIMER_0)
#define SPARKBOT_LEDC_CHANNEL     (LEDC_CHANNEL_0)

#define SPARKBOT_CAMERA_SIOD      (GPIO_NUM_NC)
#define SPARKBOT_CAMERA_SIOC      (GPIO_NUM_NC)

/* 触摸按键 */
#define TOUCH_BUTTON1_GPIO       (GPIO_NUM_1)
#define TOUCH_BUTTON2_GPIO       (GPIO_NUM_2)
#define TOUCH_BUTTON3_GPIO       (GPIO_NUM_3)

/**
 * 触摸触发「先 Capture 再 Explain」（与 MCP `self.camera.take_photo` 一致）：
 * - 键 1：短按释放（未触发长按初始化）时排队；
 * - 键 2：成功 goForward 一小步后排队。
 * 需已配置图像解释 URL/Token。置 0 可关闭。
 */
#ifndef DEEP_DOG_TOUCH2_SHORT_PHOTO_EXPLAIN
#define DEEP_DOG_TOUCH2_SHORT_PHOTO_EXPLAIN 1
#endif

#endif // _BOARD_CONFIG_H_
