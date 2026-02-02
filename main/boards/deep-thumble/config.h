#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

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

// I2C 主频率（与 BMI270 组件示例保持一致或略高）
#define I2C_MASTER_FREQ_HZ      (400 * 1000)

#define BUILTIN_LED_GPIO        GPIO_NUM_NC
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_NC
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_NC

#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  240
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false  // 不翻转整屏；仅通过摄像头 SetVFlip 翻转画面
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

// 2812灯带
#define WS2812_STRIP_GPIO GPIO_NUM_38
#define WS2812_LED_COUNT 24

#define FACE_RECOGNITION_INTERVAL_MS   2000  // 人脸识别周期（ms），测试分析时用 2s 便于看 log
#define FACE_RECOGNITION_DELAYED_START_MS 15000  // 延迟启动人脸管道（ms），让 AFE/WakeNet 先完成 32KB PSRAM 分配，避免「Item psram alloc failed」后 memcpy(NULL) 崩溃
#define FACE_RECOGNITION_TEST_PREVIEW  1     // 测试开关：1=周期显示预览并在检测到人脸时绘制人脸框，0=不显示
#define FACE_AI_PASSTHROUGH            0     // 0=正常检测+画框并送 LCD；1=仅透传（不检测、不画框），用于验证内存
#define MAX_FACE_COUNT                 5     // 本地最多保存的人脸数量
#define ENABLE_REMOTE_RECOGNITION      1     // 是否启用远程识别（当前使用本地占位实现）
#define FACE_USE_VIRTUAL_ONLY          1     // 1=新注册时屏蔽 Explain，仅用虚拟姓名，用于先验证离线人脸检测/框是否正常；0=正常走 Explain
#define REMOTE_RECOGNITION_TIMEOUT_MS  3000  // 远程识别超时时间
#define FACE_STORAGE_PATH              "/assets/face_recognition/"  // 人脸数据存储路径（占位）
#define FACE_DETECT_SCORE_THRESHOLD    0.94f  // 折中：0.96 对天花板好但人脸漏检多，0.94 减少人脸漏检；天花板误检略增可再微调
#define FACE_DETECT_MIN_BOX_SIZE       52    // 折中：55 漏人脸多，52 略降以保留略小人脸框
#define FACE_DETECT_MIN_LUMINANCE      30    // 检测输入 320×240 画面平均亮度下限（0～255）；低于此视为遮挡/过暗，本帧不认人脸
#define FACE_DETECT_BOX_SWAP_XY        0     // ESP-DL result_t 为 [x0,y0,x1,y1]，必须为 0；见 docs/face-detection-root-cause.md
#define FACE_DETECT_RGB565_BYTE_SWAP   1     // 1=检测前对 RGB565 每像素做高/低字节对调，用于排查组件 BIG_ENDIAN 与相机 LE 不一致

// 双队列架构：相机任务 + AI 任务 + 显示任务（唯一模式）
// 队列帧为 320×240×2（相机 640×480 做 2x2 下采样后入队），显著降低 PSRAM（池约 300KB vs 原 1.2MB）
#define FACE_QUEUE_FRAME_WIDTH         320   // 队列帧宽（与 FACE_DETECT_INPUT_W 一致，检测无需再缩放）
#define FACE_QUEUE_FRAME_HEIGHT        240   // 队列帧高（与 FACE_DETECT_INPUT_H 一致）
#define FACE_QUEUE_FRAME_POOL_SIZE     2     // 帧缓冲池 buffer 数量
#define FACE_QUEUE_RAW_DEPTH           2     // 原始帧队列深度
#define FACE_QUEUE_AI_DEPTH            2     // AI 处理后帧队列深度
#define FACE_QUEUE_FRAME_MAX_BYTES     (FACE_QUEUE_FRAME_WIDTH * FACE_QUEUE_FRAME_HEIGHT * 2)  // 320×240 RGB565
#define FACE_CAMERA_CAPTURE_INTERVAL_MS 500  // 取帧间隔（ms），500≈2fps，后台人脸检测/识别为主、非实时预览
#define FACE_CAMERA_TASK_STACK         4096
#define FACE_AI_TASK_STACK              4096  // 原 6144，降至 4096 以省 ~2KB 内部 RAM
#define FACE_DISPLAY_TASK_STACK        4096
#define FACE_CAMERA_TASK_PRIORITY      4
#define FACE_AI_TASK_PRIORITY          4
#define FACE_DISPLAY_TASK_PRIORITY     3

#define MOTOR_SPEED_MAX 100
#define MOTOR_SPEED_80  80
#define MOTOR_SPEED_60  60
#define MOTOR_SPEED_MIN 0

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

/* Camera PINs*/
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
#define SPARKBOT_CAMERA_XCLK      (GPIO_NUM_15)
#define SPARKBOT_CAMERA_PCLK      (GPIO_NUM_13)
#define SPARKBOT_CAMERA_VSYNC     (GPIO_NUM_6)
#define SPARKBOT_CAMERA_HSYNC     (GPIO_NUM_7)

#define SPARKBOT_CAMERA_XCLK_FREQ (16000000)
#define SPARKBOT_LEDC_TIMER       (LEDC_TIMER_0)
#define SPARKBOT_LEDC_CHANNEL     (LEDC_CHANNEL_0)

#define SPARKBOT_CAMERA_SIOD      (GPIO_NUM_NC)
#define SPARKBOT_CAMERA_SIOC      (GPIO_NUM_NC)

#endif // _BOARD_CONFIG_H_
