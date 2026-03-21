#include "wifi_board.h"
#include "codecs/es8311_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "settings.h"
#include "can/ESP32-TWAI-CAN.hpp"
#include "motor/deep_motor.h"
#include "motor/deep_motor_control.h"
#include "leg/leg_control.h"
#include "dog/dog_control.h"
#include "touch_btn/touch_button_controller.h"
#include "touch_btn/deep_dog_touch_app.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esp_video.h"

#define TAG "deep_dog"

// CAN 接收任务栈与优先级
#define CAN_RX_TASK_STACK  4096
#define CAN_RX_TASK_PRIO   5
#define DEEP_DOG_TEST_MOTOR_ID  1

class SparkBotEs8311AudioCodec : public Es8311AudioCodec {
private:    

public:
    SparkBotEs8311AudioCodec(void* i2c_master_handle, i2c_port_t i2c_port, int input_sample_rate, int output_sample_rate,
                        gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din,
                        gpio_num_t pa_pin, uint8_t es8311_addr, bool use_mclk = true)
        : Es8311AudioCodec(i2c_master_handle, i2c_port, input_sample_rate, output_sample_rate,
                             mclk,  bclk,  ws,  dout,  din,pa_pin,  es8311_addr,  use_mclk = true) {}

    void EnableOutput(bool enable) override {
        if (enable == output_enabled_) {
            return;
        }
        if (enable) {
            Es8311AudioCodec::EnableOutput(enable);
        } else {
           // Nothing todo because the display io and PA io conflict
        }
    }
};

class DeepDog : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    Button boot_button_;
    TouchButtonController touch_buttons_;
    Display* display_;
    EspVideo* camera_;
    DeepMotor* deep_motor_ = nullptr;
    DogControl dog_;  // 整机：4 条腿，内部持有 4 个 LegControl
    DeepDogTouchApp touch_app_{&dog_};  // 触摸按键业务（须在 dog_ 之后构造）
    LegControl* leg_ptrs_[4] = { nullptr };  // 供单腿 MCP 回调使用，指向 dog_ 内 4 腿
    TaskHandle_t can_rx_task_handle_ = nullptr;

    static void CanRxTask(void* arg) {
        DeepMotor* motor = static_cast<DeepMotor*>(arg);
        CanFrame frame;
        while (1) {
            if (ESP32Can.readFrame(&frame, 50)) {
                ESP_LOGI(TAG, "CAN RX: id=0x%08lX extd=%d dlc=%d data=%02X %02X %02X %02X %02X %02X %02X %02X",
                         (unsigned long)frame.identifier, frame.extd ? 1 : 0, frame.data_length_code,
                         frame.data[0], frame.data[1], frame.data[2], frame.data[3],
                         frame.data[4], frame.data[5], frame.data[6], frame.data[7]);
                if (motor) {
                    motor->processCanFrame(frame);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    void InitializeCan() {
        ESP32Can.setTxQueueSize(10);
        ESP32Can.setRxQueueSize(10);
        bool ok = ESP32Can.begin(
            ESP32Can.convertSpeed(1000),
            (int8_t)CAN_TX_GPIO,
            (int8_t)CAN_RX_GPIO,
            10,
            10
        );
        if (ok) {
            ESP_LOGI(TAG, "CAN init ok, TX=%d RX=%d", (int)CAN_TX_GPIO, (int)CAN_RX_GPIO);
        } else {
            ESP_LOGE(TAG, "CAN init failed");
        }
    }

    void InitializeI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_GPIO;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_CLK_GPIO;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

    void InitializeTouchButtons() {
        if (!touch_buttons_.Initialize(
                TOUCH_BUTTON1_GPIO, TOUCH_BUTTON2_GPIO, TOUCH_BUTTON3_GPIO,
                [this](int button_id,
                       TouchButtonEvent event,
                       uint32_t value,
                       uint32_t baseline,
                       uint32_t abs_diff) {
                    touch_app_.OnTouchEvent(button_id, event, value, baseline, abs_diff);
                })) {
            ESP_LOGE(TAG, "Touch button controller init failed");
        }
    }

    void InitializeDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_GPIO;
        io_config.dc_gpio_num = DISPLAY_DC_GPIO;
        io_config.spi_mode = 0;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片
        ESP_LOGD(TAG, "Install LCD driver");

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
        
        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, true);
        esp_lcd_panel_disp_on_off(panel, true);
        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeCamera() {

        // DVP pin configuration
        static esp_cam_ctlr_dvp_pin_config_t dvp_pin_config = {
            .data_width = CAM_CTLR_DATA_WIDTH_8,
            .data_io = {
                [0] = SPARKBOT_CAMERA_D0,
                [1] = SPARKBOT_CAMERA_D1,
                [2] = SPARKBOT_CAMERA_D2,
                [3] = SPARKBOT_CAMERA_D3,
                [4] = SPARKBOT_CAMERA_D4,
                [5] = SPARKBOT_CAMERA_D5,
                [6] = SPARKBOT_CAMERA_D6,
                [7] = SPARKBOT_CAMERA_D7,
            },
            .vsync_io = SPARKBOT_CAMERA_VSYNC,
            .de_io = SPARKBOT_CAMERA_HSYNC,
            .pclk_io = SPARKBOT_CAMERA_PCLK,
            .xclk_io = SPARKBOT_CAMERA_XCLK,
        };

        // 复用 I2C 总线
        esp_video_init_sccb_config_t sccb_config = {
            .init_sccb = false,  // 不初始化新的 SCCB，使用现有的 I2C 总线
            .i2c_handle = i2c_bus_,  // 使用现有的 I2C 总线句柄
            .freq = 100000,  // 100kHz
        };

        // DVP configuration
        esp_video_init_dvp_config_t dvp_config = {
            .sccb_config = sccb_config,
            .reset_pin = SPARKBOT_CAMERA_RESET,
            .pwdn_pin = SPARKBOT_CAMERA_PWDN,
            .dvp_pin = dvp_pin_config,
            .xclk_freq = SPARKBOT_CAMERA_XCLK_FREQ,
        };

        // Main video configuration
        esp_video_init_config_t video_config = {
            .dvp = &dvp_config,
        };
        
        camera_ = new EspVideo(video_config);

        Settings settings("sparkbot", false);
        // 考虑到部分复刻使用了不可动摄像头的设计，默认启用翻转
        bool camera_flipped = static_cast<bool>(settings.GetInt("camera-flipped", 0));
        ESP_LOGI(TAG, "Camera Flipped: %d", camera_flipped);
        camera_flipped = 0;
        camera_->SetHMirror(camera_flipped);
        camera_->SetVFlip(camera_flipped);
    }

    void InitializeTools() {
        auto& mcp_server = McpServer::GetInstance();
        // 临时屏蔽电机级 MCP（开源项目 MCP 上限约 32，优先测试腿/整机控制；需要单电机调试时取消注释）
        // RegisterMotorMcpTools(mcp_server, deep_motor_);
        dog_.setDeepMotor(deep_motor_);
        dog_.getLegs(leg_ptrs_);  // leg_ptrs_[0..3] 指向 dog_ 内 4 条腿
        RegisterLegMcpTools(mcp_server, leg_ptrs_);   // 单腿调试：self.leg.*
        RegisterDogMcpTools(mcp_server, &dog_);        // 整机：self.dog.* / self.chassis.*
    }

public:
    DeepDog() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        InitializeSpi();
        InitializeDisplay();
        InitializeButtons();
        InitializeTouchButtons();
        InitializeCamera();
        InitializeCan();
        deep_motor_ = new DeepMotor(nullptr);
        // 不再预注册电机 1：整机 12 个电机（11–13,21–23,51–53,61–63）由 dog.init() 时全部注册，槽位刚好 12，预注册会占满导致电机 63 无法注册
        InitializeTools();  // 内里会配置 leg_fl_ 并注册腿 MCP
        xTaskCreate(CanRxTask, "can_rx", CAN_RX_TASK_STACK, deep_motor_, CAN_RX_TASK_PRIO, &can_rx_task_handle_);
        GetBacklight()->RestoreBrightness();
    }

    virtual AudioCodec* GetAudioCodec() override {
         static SparkBotEs8311AudioCodec audio_codec(i2c_bus_, I2C_NUM_0, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual Camera* GetCamera() override {
        return camera_;
    }
};

DECLARE_BOARD(DeepDog);
