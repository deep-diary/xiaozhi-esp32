#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/uart.h>

/** 须在 #include "motor/motor_config.h" 之前定义：否则 motor_config 会先默认 DEEP_DOG_CAN_RX_HEX_LOG=1，此处 #ifndef 不再生效。 */
#ifndef DEEP_DOG_CAN_RX_HEX_LOG
#define DEEP_DOG_CAN_RX_HEX_LOG 0
#endif
#ifndef DEEP_DOG_CAN_TX_HEX_LOG
#define DEEP_DOG_CAN_TX_HEX_LOG 0
#endif

// 模块化配置拆分：电机相关宏移至 motor/motor_config.h（便于复用 motor/ 目录）
#include "motor/motor_config.h"
// 步态/底盘策略配置（便于复用 dog/、leg/）
#include "dog/dog_config.h"
// 轨迹插值配置（便于复用 trajectory/）
#include "trajectory/trajectory_config.h"
// 网页控制配置（便于复用 http-server/）
#include "http-server/http_server_config.h"
// 人脸检测配置（便于复用 face_ai/）
#include "face_ai/face_ai_config.h"
// 视觉 Hub / RTSP 推流（便于复用 vision/）
#include "vision/vision_config.h"
// 网络配置（便于复用 net/）
#include "net/net_config.h"
// 触摸按键行为配置（便于复用 touch_btn/）
#include "touch_btn/touch_config.h"

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
// CAN RX/TX 十六进制日志宏见文件顶部（须在 motor_config.h 之前）

/** TWAI/CAN 队列深度（增大可降低高频下发时的 send timeout/丢帧概率，代价是少量 RAM 占用） */
#ifndef DEEP_DOG_CAN_TX_QUEUE_SIZE
#define DEEP_DOG_CAN_TX_QUEUE_SIZE 96
#endif
#ifndef DEEP_DOG_CAN_RX_QUEUE_SIZE
#define DEEP_DOG_CAN_RX_QUEUE_SIZE 96
#endif

/**
 * 电机「速度档位」占位宏（0～100 整数比例），与 esp-sparkbot 等板级模板同源。
 * 当前 deep-dog 工程内 **无任何 .cc/.h 引用**（电机实际限速用 rad/s，见 protocol_motor / MCP / LegControl）。
 * 若后续做语音「百分之八十速度」、UI 滑条或步态占空比，可在此统一换算；否则可删除或保持不动。
 */
#define MOTOR_SPEED_MAX 100
#define MOTOR_SPEED_80  80
#define MOTOR_SPEED_60  60
#define MOTOR_SPEED_MIN 0

// 步态/轨迹相关宏已迁移至 dog/dog_config.h 与 trajectory/trajectory_config.h

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

// HTTP server / face_ai / net / touch 配置已拆分到各模块目录的 *_config.h

#endif // _BOARD_CONFIG_H_
