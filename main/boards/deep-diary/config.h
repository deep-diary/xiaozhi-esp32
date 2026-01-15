
#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_


#include <driver/gpio.h>

#define AUDIO_INPUT_SAMPLE_RATE      24000    //24000
#define AUDIO_OUTPUT_SAMPLE_RATE     24000   //24000

#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_3
#define AUDIO_I2S_GPIO_WS GPIO_NUM_9
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_46
#define AUDIO_I2S_GPIO_DIN GPIO_NUM_14
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_10

#define AUDIO_CODEC_I2C_SDA_PIN GPIO_NUM_41
#define AUDIO_CODEC_I2C_SCL_PIN GPIO_NUM_42
#define AUDIO_CODEC_ES8388_ADDR ES8388_CODEC_DEFAULT_ADDR

#define BOOT_BUTTON_GPIO GPIO_NUM_0

#define BUILTIN_LED_GPIO GPIO_NUM_1

#define LCD_SCLK_PIN GPIO_NUM_12
#define LCD_MOSI_PIN GPIO_NUM_11
#define LCD_MISO_PIN GPIO_NUM_13
#define LCD_DC_PIN GPIO_NUM_40
#define LCD_CS_PIN GPIO_NUM_21

#define DISPLAY_WIDTH    320
#define DISPLAY_HEIGHT   240
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY  true

#define DISPLAY_OFFSET_X 0
#define DISPLAY_OFFSET_Y 0

#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_NC
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT true

/* 相机引脚配置 */
#define CAM_PIN_PWDN    GPIO_NUM_NC
#define CAM_PIN_RESET   GPIO_NUM_NC
#define CAM_PIN_VSYNC   GPIO_NUM_47
#define CAM_PIN_HREF    GPIO_NUM_48
#define CAM_PIN_PCLK    GPIO_NUM_45
#define CAM_PIN_XCLK    GPIO_NUM_NC
#define CAM_PIN_SIOD    GPIO_NUM_39
#define CAM_PIN_SIOC    GPIO_NUM_38
#define CAM_PIN_D0      GPIO_NUM_4
#define CAM_PIN_D1      GPIO_NUM_5
#define CAM_PIN_D2      GPIO_NUM_6
#define CAM_PIN_D3      GPIO_NUM_7
#define CAM_PIN_D4      GPIO_NUM_15
#define CAM_PIN_D5      GPIO_NUM_16
#define CAM_PIN_D6      GPIO_NUM_17
#define CAM_PIN_D7      GPIO_NUM_18
#define OV_PWDN_IO      4
#define OV_RESET_IO     5

// 舵机云台控制 - 使用支持MCPWM的引脚
#define SERVO_PAN_GPIO GPIO_NUM_19   // 水平旋转，支持PWM输出
#define SERVO_TILT_GPIO GPIO_NUM_20  // 垂直旋转，支持PWM输出

// CAN总线 - 已验证可工作的配置
#define CAN_TX_GPIO GPIO_NUM_19   // TX引脚，支持输出
#define CAN_RX_GPIO GPIO_NUM_20   // RX引脚，支持输入

// 2812灯带
#define WS2812_STRIP_GPIO GPIO_NUM_8
#define WS2812_LED_COUNT 24

// 功能使能配置 - 相机功能与CAN总线、灯带功能存在GPIO冲突
#define ENABLE_CAMERA_FEATURE 1          // 1:启用相机功能, 0:禁用相机功能 
#define ENABLE_LED_STRIP_FEATURE 1       // 1:启用2812灯带功能, 0:禁用2812灯带功能
#define ENABLE_QMA6100P_FEATURE 1        // 1:启用QMA6100P加速度计, 0:禁用QMA6100P加速度计

//--------------------------------------------------
// 网络流媒体模式配置 - MJPEG服务器模式与TCP客户端模式互斥
#define ENABLE_TCP_CLIENT_MODE 1         // 1:启用TCP客户端模式, 0:禁用TCP客户端模式
#define ENABLE_MJPEG_FEATURE 0           // 1:启用MJPEG流媒体服务器, 0:禁用(计划迁移到RTSP)
//--------------------------------------------------

//--------------------------------------------------
// // CAN总线功能与云台舵机功能存在GPIO冲突
#define ENABLE_SERVO_FEATURE 1          // 1:启用云台舵机功能, 0:禁用云台舵机功能
#define ENABLE_CAN_FEATURE 0             // 1:启用CAN总线功能, 0:禁用CAN总线功能 
//--------------------------------------------------

// TCP客户端模式配置参数
#if ENABLE_TCP_CLIENT_MODE
    #define TCP_SERVER_IP "35.192.64.247"    // TCP服务器IP地址
    #define TCP_SERVER_PORT 8080             // TCP服务器端口
    
    // 当启用TCP客户端模式时，自动禁用MJPEG服务器
    #undef ENABLE_MJPEG_FEATURE
    #define ENABLE_MJPEG_FEATURE 0
#endif


#endif // _BOARD_CONFIG_H_

