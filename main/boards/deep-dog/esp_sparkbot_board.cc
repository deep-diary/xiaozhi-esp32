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
#if DEEP_DOG_WIFI_USE_STATIC_IP
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#endif
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <cstring>
#include <memory>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esp_video.h"

#if DEEP_DOG_HTTP_SERVER_ENABLE
#include "http-server/deep_dog_http_server.h"
#endif

#define TAG "deep_dog"

#if DEEP_DOG_WIFI_USE_STATIC_IP
namespace {

esp_event_handler_instance_t s_deep_dog_sta_connected_hook = nullptr;

void DeepDogApplyStaticStaIpv4() {
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == nullptr) {
        ESP_LOGW(TAG, "静态 IP: 未找到 WIFI_STA_DEF");
        return;
    }
    esp_err_t err = esp_netif_dhcpc_stop(netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGW(TAG, "dhcpc_stop: %s", esp_err_to_name(err));
    }

    esp_netif_ip_info_t ip_info = {};
    ip_info.ip.addr = ESP_IP4TOADDR(DEEP_DOG_WIFI_STATIC_IP_O1, DEEP_DOG_WIFI_STATIC_IP_O2, DEEP_DOG_WIFI_STATIC_IP_O3,
                                    DEEP_DOG_WIFI_STATIC_IP_O4);
    ip_info.gw.addr = ESP_IP4TOADDR(DEEP_DOG_WIFI_STATIC_GW_O1, DEEP_DOG_WIFI_STATIC_GW_O2, DEEP_DOG_WIFI_STATIC_GW_O3,
                                    DEEP_DOG_WIFI_STATIC_GW_O4);
    ip_info.netmask.addr = ESP_IP4TOADDR(DEEP_DOG_WIFI_STATIC_NM_O1, DEEP_DOG_WIFI_STATIC_NM_O2,
                                         DEEP_DOG_WIFI_STATIC_NM_O3, DEEP_DOG_WIFI_STATIC_NM_O4);

    err = esp_netif_set_ip_info(netif, &ip_info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "静态 IP 设置失败: %s", esp_err_to_name(err));
        return;
    }

    esp_netif_dns_info_t dns = {};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = ip_info.gw.addr;
    err = esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "DNS 设置: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "WiFi 已切静态 IP: " IPSTR " mask " IPSTR " gw " IPSTR, IP2STR(&ip_info.ip), IP2STR(&ip_info.netmask),
             IP2STR(&ip_info.gw));
}

void DeepDogOnWifiStaConnected(void* arg, esp_event_base_t base, int32_t id, void* data) {
    (void)arg;
    (void)base;
    (void)id;
    (void)data;
    DeepDogApplyStaticStaIpv4();
}

}  // namespace
#endif  // DEEP_DOG_WIFI_USE_STATIC_IP

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
#if DEEP_DOG_HTTP_SERVER_ENABLE
    std::unique_ptr<DeepDogHttpServer> http_server_;
#endif

    static void CanRxTask(void* arg) {
        DeepMotor* motor = static_cast<DeepMotor*>(arg);
        CanFrame frame;
        while (1) {
            if (ESP32Can.readFrame(&frame, 50)) {
#if DEEP_DOG_CAN_HEX_LOG
                ESP_LOGI(TAG, "CAN RX ext id=0x%08lX dlc=%u data=%02X %02X %02X %02X %02X %02X %02X %02X",
                         (unsigned long)frame.identifier, (unsigned)frame.data_length_code, frame.data[0],
                         frame.data[1], frame.data[2], frame.data[3], frame.data[4], frame.data[5], frame.data[6],
                         frame.data[7]);
#else
                ESP_LOGD(TAG, "CAN RX id=0x%08lX dlc=%u data=%02X %02X %02X %02X %02X %02X %02X %02X",
                         (unsigned long)frame.identifier, (unsigned)frame.data_length_code, frame.data[0],
                         frame.data[1], frame.data[2], frame.data[3], frame.data[4], frame.data[5], frame.data[6],
                         frame.data[7]);
#endif
                if (motor) {
                    motor->processCanFrame(frame);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    void InitializeCan() {
        // 连续行走时每个小步会突发下发 12 个关节帧；队列太小容易触发 send 超时。
        // 适当增大 TX/RX 队列可显著降低高频下的丢帧/超时概率。
        ESP32Can.setTxQueueSize(64);
        ESP32Can.setRxQueueSize(64);
        bool ok = ESP32Can.begin(
            ESP32Can.convertSpeed(1000),
            (int8_t)CAN_TX_GPIO,
            (int8_t)CAN_RX_GPIO,
            64,
            64
        );
        if (ok) {
            ESP_LOGI(TAG, "CAN init ok, TX=%d RX=%d", (int)CAN_TX_GPIO, (int)CAN_RX_GPIO);
#if DEEP_DOG_CAN_HEX_LOG
            ESP_LOGI(TAG, "CAN RX 报文日志已开启 (DEEP_DOG_CAN_HEX_LOG)：每条接收帧打印 ext id / dlc / data[0..7]");
#endif
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

        // 触摸/显示等初始化后给 SCCB 总线与传感器上电稳定一点时间，避免首读 PID=0
        vTaskDelay(pdMS_TO_TICKS(80));

        camera_ = new EspVideo(video_config);

        Settings settings("sparkbot", false);
        // 考虑到部分复刻使用了不可动摄像头的设计，默认启用翻转
        bool camera_flipped = static_cast<bool>(settings.GetInt("camera-flipped", 0));
        ESP_LOGI(TAG, "Camera Flipped: %d", camera_flipped);
        camera_flipped = 1;
        camera_->SetHMirror(camera_flipped);
        camera_->SetVFlip(camera_flipped);
        touch_app_.SetCamera(camera_);
#if DEEP_DOG_HTTP_SERVER_ENABLE
        // 须在 esp_netif_init / tcpip 就绪之后启动（见 StartNetwork）；勿在构造函数里 httpd_start
        http_server_ = std::make_unique<DeepDogHttpServer>(camera_, &dog_, DEEP_DOG_HTTP_SERVER_PORT);
#endif
    }

    void InitializeTools() {
        auto& mcp_server = McpServer::GetInstance();
        dog_.setDeepMotor(deep_motor_);
        dog_.getLegs(leg_ptrs_);
        // MCP 数量上限约 32：单电机/MIT 调试时只开电机工具，腿/整机先注释，需要整机时再改回
        // RegisterMotorMcpTools(mcp_server, deep_motor_);
        // RegisterLegMcpTools(mcp_server, leg_ptrs_);
        RegisterDogMcpTools(mcp_server, &dog_);
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

    void StartNetwork() override {
        WifiBoard::StartNetwork();
#if DEEP_DOG_WIFI_USE_STATIC_IP
        if (s_deep_dog_sta_connected_hook == nullptr) {
            esp_err_t er = esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, DeepDogOnWifiStaConnected,
                                                               nullptr, &s_deep_dog_sta_connected_hook);
            if (er != ESP_OK) {
                ESP_LOGE(TAG, "注册 STA_CONNECTED 静态 IP: %s", esp_err_to_name(er));
            }
        }
        wifi_ap_record_t ap{};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            DeepDogApplyStaticStaIpv4();
        }
#endif
#if DEEP_DOG_HTTP_SERVER_ENABLE
        if (http_server_ && !http_server_->IsRunning()) {
            if (!http_server_->Start()) {
                ESP_LOGW(TAG, "HTTP 控制/MJPEG 服务启动失败（可检查端口占用）");
            }
        }
#endif
    }
};

DECLARE_BOARD(DeepDog);
