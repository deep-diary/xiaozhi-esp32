#include "wifi_board.h"
#include "board.h"
#include "codecs/es8311_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "net/net_config.h"
#include "net/deep_dog_sntp.h"
#include "mcp_server.h"
#include "ws-mcp/ws_mcp_config.h"
#if DEEP_DOG_WS_MCP_ENABLE
#include "ws-mcp/deep_dog_ws_mcp_server.h"
#endif
#include "settings.h"
#include "touch_btn/touch_button_controller.h"
#include "touch_btn/touch_event_hub.h"
#include "touch_btn/touch_app_dispatcher.h"
#include "touch_btn/touch_config.h"
#if DEEP_DOG_TOUCH_COMBO_ENABLE
#include "touch_btn/touch_combo_recognizer.h"
#endif
#if DEEP_DOG_TOUCH_APP_LOG_ENABLE
#include "touch_btn/apps/touch_app_log.h"
#endif
#if DEEP_DOG_TOUCH_APP_DOG_ENABLE
#include "touch_btn/apps/touch_app_dog.h"
#endif
#if DEEP_DOG_TOUCH_APP_SERVO_ENABLE
#include "touch_btn/apps/touch_app_servo.h"
#endif

#include "handle/handle_config.h"
#if DEEP_DOG_HANDLE_ENABLE
#include "handle/handle_event_hub.h"
#include "handle/handle_app_dispatcher.h"
#include "handle/sources/handle_bt.h"
#if DEEP_DOG_HANDLE_APP_LOG_ENABLE
#include "handle/apps/handle_app_log.h"
#endif
#if DEEP_DOG_HANDLE_APP_DOG_ENABLE
#include "handle/apps/handle_app_dog.h"
#endif
#if DEEP_DOG_HANDLE_APP_SERVO_ENABLE
#include "handle/apps/handle_app_servo.h"
#endif
#if DEEP_DOG_HANDLE_APP_KEYMAP_ENABLE
#include "handle/apps/handle_app_keymap.h"
#include "handle/keymap_store.h"
#endif
#endif

#if DEEP_DOG_CAN_ENABLE
#include "can/can_config.h"
#include "can/can_frame_hub.h"
#include "can/ESP32-TWAI-CAN.hpp"
#endif
#if DEEP_DOG_MOTOR_ENABLE
#include "motor/deep_motor.h"
#include "motor/deep_motor_control.h"
#include "motor/motor_access.h"
#endif
#if DEEP_DOG_DOG_ENABLE
#include "leg/leg_control.h"
#include "dog/dog_control.h"
#endif
#if DEEP_DOG_UART_ENABLE
#include "uart/uart_stub.h"
#endif
#if DEEP_DOG_LED_ENABLE
#include "led/led_init.h"
#include "led/led_mcp.h"
#endif
#if DEEP_DOG_SERVO_ENABLE || DEEP_DOG_GIMBAL_ENABLE
#include "servo/servo_config.h"
#include "servo/servo_control.h"
#include "gimbal/Gimbal.h"
#endif
#if DEEP_DOG_ARM_ENABLE
#include "arm/arm_stub.h"
#endif
#if DEEP_DOG_RS485_ENABLE
#include "rs485/rs485_stub.h"
#endif
#if DEEP_DOG_IO_ENABLE
#include "io_ext/io_ext_stub.h"
#endif
#if DEEP_DOG_AD_ENABLE
#include "ad/ad_stub.h"
#endif

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_idf_version.h>
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

#include "i2c_bus.h"
#include "esp_video.h"

#include "vision/vision_config.h"
#include "face_ai_config.h"
#include "sensor/imu_config.h"
#if DEEP_DOG_IMU_ENABLE
#include "sensor/imu_sensor.h"
#include "sensor/imu_switch.h"
#endif

#include <wifi_manager.h>
#include <esp_heap_caps.h>

#if DEEP_DOG_HTTP_SERVER_ENABLE
#include "http-server/http_server_config.h"
#include "http-server/deep_dog_http_server.h"
#if DEEP_DOG_DOG_ENABLE
#include "dog/dog_control.h"
#endif
#endif
#if DEEP_DOG_VISION_HUB_ENABLE
#include "vision/vision_frame_hub.h"
#endif
#if DEEP_DOG_FACE_AI_ENABLE
#include "face_ai_bridge.h"
#include "face_greet.h"
#endif
#include "mqtt/memory_report.h"
#include "mqtt/mqtt_config.h"
#if DEEP_DOG_MQTT_ENABLE
#include "mqtt/deep_dog_mqtt.h"
#include "pairing/pairing_mcp.h"
#if DEEP_DOG_FACE_AI_ENABLE
#include "face_mcp.h"
#include "board_diagnostics_mcp.h"
#endif
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

#if DEEP_DOG_CAN_ENABLE
#define CAN_RX_TASK_STACK  4096
#define CAN_RX_TASK_PRIO   5
#endif

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
    i2c_bus_handle_t shared_i2c_bus_handle_ = nullptr;
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    Button boot_button_;
    TouchButtonController touch_buttons_;
    TouchEventHub touch_hub_;
    TouchAppDispatcher touch_dispatcher_{&touch_hub_};
#if DEEP_DOG_TOUCH_COMBO_ENABLE
    TouchComboRecognizer touch_combo_;
#endif
#if DEEP_DOG_TOUCH_APP_LOG_ENABLE
    TouchAppLog touch_app_log_;
#endif
#if DEEP_DOG_DOG_ENABLE
    DogControl dog_;
#if DEEP_DOG_TOUCH_APP_DOG_ENABLE
    TouchAppDog touch_app_dog_{&dog_};
#endif
    LegControl* leg_ptrs_[4] = { nullptr };
#else
#if DEEP_DOG_TOUCH_APP_DOG_ENABLE
    TouchAppDog touch_app_dog_{};
#endif
#endif
#if DEEP_DOG_TOUCH_APP_SERVO_ENABLE
    TouchAppServo touch_app_servo_;
#endif
#if DEEP_DOG_HANDLE_ENABLE
    HandleEventHub handle_hub_;
    HandleAppDispatcher handle_dispatcher_{&handle_hub_};
#if DEEP_DOG_HANDLE_APP_LOG_ENABLE
    HandleAppLog handle_app_log_;
#endif
#if DEEP_DOG_HANDLE_APP_DOG_ENABLE
#if DEEP_DOG_DOG_ENABLE
    HandleAppDog handle_app_dog_{&dog_, &handle_hub_};
#else
    HandleAppDog handle_app_dog_{&handle_hub_};
#endif
#endif
#if DEEP_DOG_HANDLE_APP_SERVO_ENABLE
    HandleAppServo handle_app_servo_;
#endif
#if DEEP_DOG_HANDLE_APP_KEYMAP_ENABLE
    HandleAppKeyMap handle_app_keymap_{&handle_hub_};
#endif
#endif
    Display* display_ = nullptr;
    EspVideo* camera_ = nullptr;
#if DEEP_DOG_MOTOR_ENABLE
    DeepMotor* deep_motor_ = nullptr;
#endif
#if DEEP_DOG_CAN_ENABLE && DEEP_DOG_MOTOR_ENABLE
    TaskHandle_t can_rx_task_handle_ = nullptr;
#endif
#if DEEP_DOG_IMU_ENABLE
    std::unique_ptr<DeepDogImuSensor> imu_sensor_;
    std::unique_ptr<DeepDogImuSwitch> imu_switch_;
#endif
#if DEEP_DOG_VISION_HUB_ENABLE
    std::unique_ptr<VisionFrameHub> vision_hub_;
#endif
#if DEEP_DOG_HTTP_SERVER_ENABLE
    std::unique_ptr<DeepDogHttpServer> http_server_;
#endif
#if DEEP_DOG_MQTT_ENABLE
    std::unique_ptr<DeepDogMqtt> board_mqtt_;
#endif

#if DEEP_DOG_CAN_ENABLE
    void InitializeCan() {
        ESP32Can.setTxQueueSize(DEEP_DOG_CAN_TX_QUEUE_SIZE);
        ESP32Can.setRxQueueSize(DEEP_DOG_CAN_RX_QUEUE_SIZE);
        bool ok = ESP32Can.begin(
            ESP32Can.convertSpeed(1000),
            (int8_t)CAN_TX_GPIO,
            (int8_t)CAN_RX_GPIO,
            DEEP_DOG_CAN_TX_QUEUE_SIZE,
            DEEP_DOG_CAN_RX_QUEUE_SIZE
        );
        if (ok) {
            ESP_LOGI(TAG, "CAN init ok, TX=%d RX=%d, queue tx=%d rx=%d",
                     (int)CAN_TX_GPIO, (int)CAN_RX_GPIO,
                     (int)DEEP_DOG_CAN_TX_QUEUE_SIZE, (int)DEEP_DOG_CAN_RX_QUEUE_SIZE);
#if DEEP_DOG_CAN_RX_HEX_LOG
            ESP_LOGI(TAG, "CAN RX 报文日志已开启 (DEEP_DOG_CAN_RX_HEX_LOG)");
#endif
#if DEEP_DOG_CAN_TX_HEX_LOG
            ESP_LOGI(TAG, "CAN TX 报文日志已开启 (DEEP_DOG_CAN_TX_HEX_LOG)");
#endif
        } else {
            ESP_LOGE(TAG, "CAN init failed");
        }
    }
#endif

#if DEEP_DOG_CAN_ENABLE && DEEP_DOG_MOTOR_ENABLE
    static void CanRxTask(void* arg) {
        DeepMotor* motor = static_cast<DeepMotor*>(arg);
        CanFrame frame;
        while (1) {
            if (ESP32Can.readFrame(&frame, 50)) {
                DeepDogCanNotifyFrame(&frame, 0);
#if DEEP_DOG_CAN_RX_HEX_LOG
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
                    if (motor->processCanFrame(frame)) {
#if DEEP_DOG_CAN_RX_HEX_LOG
                        const uint8_t parsed_motor_id = RX_29ID_DISASSEMBLE_MOTOR_ID(frame.identifier);
                        motor_status_t st = {};
                        if (motor->getMotorStatus(parsed_motor_id, &st) && st.has_feedback) {
                            ESP_LOGI(TAG,
                                     "CAN RX parsed motor=%u pos=%.4f rad speed=%.3f rad/s torque=%.3f N·m temp=%.1f°C seq=%lu",
                                     (unsigned)parsed_motor_id,
                                     (double)st.current_angle,
                                     (double)st.current_speed,
                                     (double)st.current_torque,
                                     (double)st.current_temp,
                                     (unsigned long)st.feedback_seq);
                        }
#endif
                    }
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
#endif

    void InitializeExtPinModules() {
        ESP_LOGI(TAG, "ext_pins mode=%s A=%d B=%d can=%d motor=%d dog=%d arm=%d servo=%d gimbal=%d led=%d",
                 DEEP_DOG_EXT_PIN_MODE_STR, (int)DEEP_DOG_EXT_PIN_A_GPIO, (int)DEEP_DOG_EXT_PIN_B_GPIO,
                 DEEP_DOG_CAN_ENABLE, DEEP_DOG_MOTOR_ENABLE, DEEP_DOG_DOG_ENABLE, DEEP_DOG_ARM_ENABLE,
                 DEEP_DOG_SERVO_ENABLE, DEEP_DOG_GIMBAL_ENABLE, DEEP_DOG_LED_ENABLE);
#if DEEP_DOG_UART_ENABLE
        DeepDogUartInit();
#endif
#if DEEP_DOG_RS485_ENABLE
        DeepDogRs485Init();
#endif
#if DEEP_DOG_IO_ENABLE
        DeepDogIoExtInit();
#endif
#if DEEP_DOG_AD_ENABLE
        DeepDogAdInit();
#endif
#if DEEP_DOG_LED_ENABLE
        DeepDogLedInit();
#endif
#if DEEP_DOG_GIMBAL_ENABLE
        if (DeepDogGimbalInit() != ESP_OK) {
            ESP_LOGW(TAG, "Gimbal init failed");
        }
#elif DEEP_DOG_SERVO_ENABLE
        if (DeepDogServoInit() != ESP_OK) {
            ESP_LOGW(TAG, "Servo bank init failed");
        }
#endif
#if DEEP_DOG_ARM_ENABLE
        DeepDogArmInit();
#endif
    }

    void InitializeI2c() {
        // 先建 master bus，再交给 i2c_bus 包装：避免 i2c_bus_v2 探测空端口时刷 ERROR
        i2c_master_bus_config_t bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags =
                {
                    .enable_internal_pullup = true,
                },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &i2c_bus_));

        i2c_config_t i2c_cfg = {
            .mode = I2C_MODE_MASTER,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .sda_pullup_en = GPIO_PULLUP_ENABLE,
            .scl_pullup_en = GPIO_PULLUP_ENABLE,
            .master =
                {
                    .clk_speed = I2C_MASTER_FREQ_HZ,
                },
            .clk_flags = 0,
        };
        shared_i2c_bus_handle_ = i2c_bus_create(I2C_NUM_0, &i2c_cfg);
        if (!shared_i2c_bus_handle_) {
            ESP_LOGE(TAG, "Failed to create shared I2C bus");
            ESP_ERROR_CHECK(ESP_FAIL);
        }
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0) && !CONFIG_I2C_BUS_BACKWARD_CONFIG
        i2c_master_bus_handle_t wrapped = i2c_bus_get_internal_bus_handle(shared_i2c_bus_handle_);
        if (wrapped) {
            i2c_bus_ = wrapped;
        }
#else
#error "deep-dog requires i2c_bus_get_internal_bus_handle()"
#endif
        if (!i2c_bus_) {
            ESP_LOGE(TAG, "Failed to obtain master bus handle from i2c_bus");
            ESP_ERROR_CHECK(ESP_FAIL);
        }
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
        if (!touch_hub_.Init()) {
            ESP_LOGE(TAG, "TouchEventHub init failed");
            return;
        }

#if DEEP_DOG_TOUCH_COMBO_ENABLE
        touch_dispatcher_.SetComboRecognizer(&touch_combo_);
#if DEEP_DOG_MQTT_ENABLE
        touch_dispatcher_.SetComboHitListener([this](const char* id) {
            if (board_mqtt_) {
                board_mqtt_->NotifyTouchCombo(id);
#if DEEP_DOG_MQTT_ENABLE
                if (strcmp(id, "hold1_tap2") == 0) {
                    board_mqtt_->StartPairingSessionOrAnnounceBound();
                } else if (strcmp(id, "hold1_tap3") == 0) {
                    board_mqtt_->RequestDeviceUnbind();
                }
#endif
            }
        });
#endif
#endif
#if DEEP_DOG_TOUCH_APP_LOG_ENABLE
        touch_dispatcher_.Register(&touch_app_log_);
#endif
#if DEEP_DOG_TOUCH_APP_DOG_ENABLE
        touch_dispatcher_.Register(&touch_app_dog_);
#endif
#if DEEP_DOG_TOUCH_APP_SERVO_ENABLE
        touch_dispatcher_.Register(&touch_app_servo_);
#endif

        if (!touch_buttons_.Initialize(
                TOUCH_BUTTON1_GPIO, TOUCH_BUTTON2_GPIO, TOUCH_BUTTON3_GPIO,
                [this](int button_id,
                       TouchButtonEvent event,
                       uint32_t value,
                       uint32_t baseline,
                       uint32_t abs_diff) {
                    TouchEvent ev;
                    ev.button_id = button_id;
                    ev.event = event;
                    ev.value = value;
                    ev.baseline = baseline;
                    ev.abs_diff = abs_diff;
                    ev.pressed_mask = touch_buttons_.GetPressedMask();
                    touch_hub_.Push(ev);
                })) {
            ESP_LOGE(TAG, "Touch button controller init failed");
            return;
        }

        if (!touch_buttons_.LoadThresholdsFromNvs()) {
            ESP_LOGI(TAG, "Touch thresholds: factory defaults (no NVS)");
        }

        if (!touch_dispatcher_.StartPeriodic(DEEP_DOG_TOUCH_DISPATCH_INTERVAL_US)) {
            ESP_LOGE(TAG, "TouchAppDispatcher timer start failed");
        }
    }

#if DEEP_DOG_HANDLE_ENABLE
    void InitializeHandle() {
        if (!handle_hub_.Init()) {
            ESP_LOGE(TAG, "HandleEventHub init failed");
            return;
        }
#if DEEP_DOG_HANDLE_APP_LOG_ENABLE
        handle_dispatcher_.Register(&handle_app_log_);
#endif
#if DEEP_DOG_HANDLE_APP_DOG_ENABLE
        handle_dispatcher_.Register(&handle_app_dog_);
#endif
#if DEEP_DOG_HANDLE_APP_SERVO_ENABLE
        handle_dispatcher_.Register(&handle_app_servo_);
#endif
#if DEEP_DOG_HANDLE_APP_KEYMAP_ENABLE
        HandleKeymapInit();
        handle_dispatcher_.Register(&handle_app_keymap_);
#endif
        if (!handle_dispatcher_.StartPeriodic(DEEP_DOG_HANDLE_DISPATCH_INTERVAL_US)) {
            ESP_LOGE(TAG, "HandleAppDispatcher timer start failed");
        }
        // BT 在 StartNetwork 里先于 WiFi 拉起（HCI 要 DMA）；AFE 可走 PSRAM 栈回退
        ESP_LOGI(TAG, "Handle input ready (BT=%d mqtt_input=%d; BT before WiFi)",
                 DEEP_DOG_HANDLE_BT_ENABLE, DEEP_DOG_HANDLE_MQTT_INPUT_ENABLE);
    }
#endif

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

        // 触摸/显示等初始化后给 SCCB 与传感器稳定时间；软重启无 PWDN 时更易首读 PID 错乱。
        vTaskDelay(pdMS_TO_TICKS(DEEP_DOG_CAMERA_INIT_DELAY_MS));

        for (int attempt = 1; attempt <= DEEP_DOG_CAMERA_INIT_RETRIES; ++attempt) {
            if (camera_ != nullptr) {
                delete camera_;
                camera_ = nullptr;
            }
            ESP_LOGI(TAG, "Camera init attempt %d/%d", attempt, DEEP_DOG_CAMERA_INIT_RETRIES);
            camera_ = new EspVideo(video_config);
            if (camera_->IsReady()) {
                ESP_LOGI(TAG, "Camera ready on attempt %d", attempt);
                break;
            }
            ESP_LOGW(TAG, "Camera not ready (attempt %d/%d)", attempt, DEEP_DOG_CAMERA_INIT_RETRIES);
            if (attempt < DEEP_DOG_CAMERA_INIT_RETRIES) {
                vTaskDelay(pdMS_TO_TICKS(DEEP_DOG_CAMERA_INIT_RETRY_GAP_MS * attempt));
            }
        }
        if (camera_ == nullptr || !camera_->IsReady()) {
            ESP_LOGE(TAG,
                     "Camera init failed after %d attempts (check power/FPC/XCLK); "
                     "vision hub will idle until power cycle",
                     DEEP_DOG_CAMERA_INIT_RETRIES);
        }

        if (camera_ != nullptr) {
            camera_->SetHMirror(DEEP_DOG_CAMERA_H_MIRROR != 0);
            camera_->SetVFlip(DEEP_DOG_CAMERA_V_FLIP != 0);
            ESP_LOGI(TAG, "Camera orient h_mirror=%d v_flip=%d", DEEP_DOG_CAMERA_H_MIRROR ? 1 : 0,
                     DEEP_DOG_CAMERA_V_FLIP ? 1 : 0);
        }
#if DEEP_DOG_TOUCH_APP_DOG_ENABLE
        touch_app_dog_.SetCamera(camera_);
#endif
#if DEEP_DOG_VISION_HUB_ENABLE
        vision_hub_ = std::make_unique<VisionFrameHub>(camera_);
#endif
#if DEEP_DOG_HTTP_SERVER_ENABLE
        // 须在 esp_netif_init / tcpip 就绪之后启动（见 StartNetwork）；勿在构造函数里 httpd_start
#if DEEP_DOG_DOG_ENABLE
        http_server_ = std::make_unique<DeepDogHttpServer>(camera_, &dog_, DEEP_DOG_HTTP_SERVER_PORT);
#else
        http_server_ = std::make_unique<DeepDogHttpServer>(camera_, nullptr, DEEP_DOG_HTTP_SERVER_PORT);
#endif
#if DEEP_DOG_VISION_HUB_ENABLE
        http_server_->SetVisionHub(vision_hub_.get());
#endif
#endif
#if DEEP_DOG_MQTT_ENABLE
        board_mqtt_ = std::make_unique<DeepDogMqtt>();
        board_mqtt_->SetTouchHub(&touch_hub_);
        board_mqtt_->SetTouchController(&touch_buttons_);
#if DEEP_DOG_HANDLE_ENABLE
        board_mqtt_->SetHandleHub(&handle_hub_);
#endif
#if DEEP_DOG_TOUCH_COMBO_ENABLE
        board_mqtt_->SetTouchComboRecognizer(&touch_combo_);
#endif
#if DEEP_DOG_VISION_HUB_ENABLE
        board_mqtt_->SetVisionHub(vision_hub_.get());
#endif
#if DEEP_DOG_HTTP_SERVER_ENABLE
        board_mqtt_->SetHttpServer(http_server_.get());
        board_mqtt_->SetHttpPort(DEEP_DOG_HTTP_SERVER_PORT);
#endif
#endif
    }

    /** WiFi IP 就绪后启动 Face/MQTT/HTTP 等（或延后至 OnActivationComplete）。 */
    void StartPostActivationServices() {
        ESP_LOGI(TAG, "post-activation: start heavy services");
        DeepDogMemoryReportLog("post_activation");
#if DEEP_DOG_MQTT_ENABLE
        if (board_mqtt_) {
            if (!board_mqtt_->IsRunning()) {
                if (!board_mqtt_->Start()) {
                    ESP_LOGW(TAG, "板级 MQTT 首连失败（将后台重连 broker）");
                }
            }
            // Application idle 已切 LOW_POWER；板级 MQTT/手柄需立刻拉回 PERFORMANCE
            SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
            ESP_LOGI(TAG, "WiFi PERFORMANCE for board MQTT / handle");
        }
#endif
#if DEEP_DOG_HTTP_SERVER_ENABLE
        if (http_server_ && !http_server_->IsRunning()) {
            if (!http_server_->Start()) {
                ESP_LOGW(TAG, "HTTP 控制/MJPEG 服务启动失败（可检查端口占用）");
            }
        }
#endif
#if DEEP_DOG_WS_MCP_ENABLE
        {
            static DeepDogWsMcpServer ws_mcp;
            if (ws_mcp.Start()) {
                ESP_LOGI(TAG, "WS MCP on port %u path %s", (unsigned)ws_mcp.Port(), ws_mcp.Path());
                DeepDogWsMcpServer* ws_ptr = &ws_mcp;
                Application::GetInstance().RegisterMcpBroadcastCallback([ws_ptr](const std::string& payload) {
                    ws_ptr->BroadcastMessage(payload);
                });
#if DEEP_DOG_MQTT_ENABLE
                if (board_mqtt_) {
                    board_mqtt_->SetWsMcpEndpoint(ws_mcp.Port(), ws_mcp.Path());
                }
#endif
            } else {
                ESP_LOGW(TAG, "WS MCP 启动失败");
            }
        }
#endif
#if DEEP_DOG_FACE_AI_ENABLE
#if DEEP_DOG_FACE_AI_DEFAULT_ENABLED
        if (DeepDogMemoryWaitInternalReady("face_ai", DEEP_DOG_BOOT_MIN_INTERNAL_FREE,
                                            DEEP_DOG_BOOT_MIN_INTERNAL_LARGEST,
                                            DEEP_DOG_BOOT_MEMORY_WAIT_MS)) {
            if (!DeepDogFaceAiRuntimeStart()) {
                ESP_LOGW(TAG, "人脸 runtime 未启动（静默识别不可用）");
            } else {
                DeepDogMemoryReportLog("face_ready");
            }
        } else {
            ESP_LOGW(TAG, "Face AI skipped: internal heap not ready after activation");
        }
#else
        ESP_LOGI(TAG, "Face AI off at boot (face/cmd enabled=true to start)");
#endif
#endif
#if DEEP_DOG_VISION_HUB_ENABLE
        if (vision_hub_ && !vision_hub_->IsRunning()) {
            if (!vision_hub_->Start()) {
                ESP_LOGW(TAG, "VisionFrameHub 启动失败");
            }
        }
#endif
    }

    void InitializeImu() {
#if DEEP_DOG_IMU_ENABLE
        if (!shared_i2c_bus_handle_) {
            ESP_LOGW(TAG, "IMU skipped: no I2C bus");
            return;
        }
        imu_sensor_ = std::make_unique<DeepDogImuSensor>(shared_i2c_bus_handle_);
        if (!imu_sensor_->Initialize()) {
            ESP_LOGW(TAG, "BMI270 init failed — MQTT imu/status will publish ok=false");
        } else {
            ESP_LOGI(TAG, "BMI270 ready");
        }
        imu_switch_ = std::make_unique<DeepDogImuSwitch>(imu_sensor_.get());
        if (!imu_switch_->Start()) {
            ESP_LOGW(TAG, "IMU switch sampler failed to start");
        }
#if DEEP_DOG_MQTT_ENABLE
        if (board_mqtt_) {
            board_mqtt_->SetImuSensor(imu_sensor_.get());
            board_mqtt_->SetImuSwitch(imu_switch_.get());
        }
#endif
#endif
    }

    void InitializeTools() {
        auto& mcp_server = McpServer::GetInstance();
#if DEEP_DOG_DOG_ENABLE
        dog_.setDeepMotor(deep_motor_);
        dog_.getLegs(leg_ptrs_);
        RegisterDogMcpTools(mcp_server, &dog_);
#elif DEEP_DOG_MOTOR_ENABLE
        RegisterMotorMcpTools(mcp_server, deep_motor_);
#endif
#if DEEP_DOG_LED_ENABLE
        RegisterLedMcpTools(mcp_server, DeepDogLedGetControl());
#endif
#if DEEP_DOG_SERVO_ENABLE
        RegisterServoMcpTools(mcp_server);
#endif
#if DEEP_DOG_MQTT_ENABLE
        if (board_mqtt_) {
            RegisterPairingMcpTools(mcp_server, board_mqtt_.get());
        }
#endif
#if DEEP_DOG_FACE_AI_ENABLE
        RegisterFaceMcpTools(mcp_server);
#endif
        RegisterBoardDiagnosticsMcpTools(mcp_server);
#if !DEEP_DOG_DOG_ENABLE && !DEEP_DOG_MOTOR_ENABLE && !DEEP_DOG_LED_ENABLE && !DEEP_DOG_SERVO_ENABLE && !DEEP_DOG_MQTT_ENABLE && !DEEP_DOG_FACE_AI_ENABLE
        (void)mcp_server;
#endif
    }

public:
    DeepDog() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        InitializeSpi();
        InitializeDisplay();
        // 背光尽早恢复：勿等触摸校准/摄像头重试（否则黑屏约 7s）
        GetBacklight()->RestoreBrightness();
        InitializeButtons();
        InitializeTouchButtons();
#if DEEP_DOG_HANDLE_ENABLE
        InitializeHandle();
#endif
        InitializeCamera();
        InitializeExtPinModules();
#if DEEP_DOG_MQTT_ENABLE && DEEP_DOG_LED_ENABLE
        if (board_mqtt_) {
            board_mqtt_->SetLedControl(DeepDogLedGetControl());
        }
#endif
#if DEEP_DOG_CAN_ENABLE
        InitializeCan();
#endif
#if DEEP_DOG_MOTOR_ENABLE
        deep_motor_ = new DeepMotor(nullptr);
        DeepDogMotorSet(deep_motor_);
#if DEEP_DOG_MQTT_ENABLE
        if (board_mqtt_) {
            board_mqtt_->SetDeepMotor(deep_motor_);
        }
#endif
#if DEEP_DOG_CAN_ENABLE
        xTaskCreate(CanRxTask, "can_rx", CAN_RX_TASK_STACK, deep_motor_, CAN_RX_TASK_PRIO, &can_rx_task_handle_);
#endif
#endif
        InitializeTools();
    }

    virtual AudioCodec* GetAudioCodec() override {
        // GPIO46 同时是背光 PWM 与 PA：传 AUDIO_CODEC_PA_PIN 会让 es8311_codec_new
        // 把脚改成 GPIO 并拉低 → 已亮的背光闪黑。PA 改由背光 PWM 兼驱（与官方「勿关 PA」同理）。
         static SparkBotEs8311AudioCodec audio_codec(i2c_bus_, I2C_NUM_0, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
            GPIO_NUM_NC, AUDIO_CODEC_ES8311_ADDR);
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

#if DEEP_DOG_MQTT_ENABLE
    /**
     * 板级 MQTT（手柄 input / 心跳）需要低延迟 TCP。
     * Application idle 会切 LOW_POWER(MAX_MODEM)，易触发 Poll/Write timeout → 断连失控。
     * 见 swrs/mqtt/M01 §Broker 稳连。
     */
    void SetPowerSaveLevel(PowerSaveLevel level) override {
        if (level == PowerSaveLevel::LOW_POWER) {
            level = PowerSaveLevel::PERFORMANCE;
            ESP_LOGD(TAG, "MQTT on: keep WiFi PERFORMANCE (ignore LOW_POWER)");
        }
        WifiBoard::SetPowerSaveLevel(level);
    }
#endif

    // [deep-dog] N03：配网页高级项展示 OTA URL + 板级 MQTT broker
    void CustomizeWifiManagerConfig(WifiManagerConfig& config) override {
        config.show_ota_config = true;
        config.show_mqtt_broker_config = true;
    }

    void StartNetwork() override {
#if DEEP_DOG_HANDLE_ENABLE && DEEP_DOG_HANDLE_BT_ENABLE
        // HCI 需要大块 INTERNAL|DMA；idle 后再启常 NO_MEM。先起 BT 占住 DMA。
        // AFE 任务栈在公共代码里对 INTERNAL 失败时回退 PSRAM（见 afe_audio_engine.cc）。
        if (!HandleBtStart(&handle_hub_)) {
            ESP_LOGE(TAG, "HandleBtStart failed (before WiFi)");
        } else {
            ESP_LOGI(TAG, "Handle BT up before WiFi (AFE may use PSRAM stack)");
        }
#endif
        WifiBoard::StartNetwork();
        DeepDogSntpInit();
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
        // 等 IP 再启 face/hub/http/mqtt：避免 WiFi/NVS 与 facedb(FAT) 并发踩 flash 断言崩溃。
        // 配网 AP 模式无 STA IP，不可死等满超时（约 30s）。
        {
            auto& wifi = WifiManager::GetInstance();
            std::string ip;
            if (wifi.IsConfigMode() ||
                Application::GetInstance().GetDeviceState() == kDeviceStateWifiConfiguring) {
                ESP_LOGI(TAG, "配网模式跳过等待 STA IP");
            } else {
                for (int i = 0; i < 60; ++i) {
                    if (wifi.IsConfigMode() ||
                        Application::GetInstance().GetDeviceState() == kDeviceStateWifiConfiguring) {
                        ESP_LOGI(TAG, "配网模式跳过等待 STA IP");
                        ip.clear();
                        break;
                    }
                    ip = wifi.GetIpAddress();
                    if (!ip.empty() && ip != "0.0.0.0") {
                        break;
                    }
                    vTaskDelay(pdMS_TO_TICKS(500));
                }
                if (ip.empty() || ip == "0.0.0.0") {
                    ESP_LOGW(TAG, "WiFi IP 超时未就绪，仍继续启动视觉/MQTT");
                } else {
                    ESP_LOGI(TAG, "WiFi IP=%s，启动 face/hub/http/mqtt/imu", ip.c_str());
                }
            }
        }
#if DEEP_DOG_IMU_ENABLE
        InitializeImu();
#endif
#if DEEP_DOG_FACE_AI_ENABLE
        DeepDogFaceGreetInit();
#if !DEEP_DOG_BOOT_DEFER_HEAVY_UNTIL_ACTIVATION
        DeepDogMemoryReportLog("boot_baseline");
#endif
#endif
#if DEEP_DOG_FACE_AI_ENABLE && !DEEP_DOG_BOOT_DEFER_HEAVY_UNTIL_ACTIVATION
#if DEEP_DOG_MQTT_ENABLE
        if (board_mqtt_) {
            if (!board_mqtt_->IsRunning()) {
                if (!board_mqtt_->Start()) {
                    ESP_LOGW(TAG, "板级 MQTT 首连失败（将后台重连 broker）");
                }
            }
            SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
            ESP_LOGI(TAG, "WiFi PERFORMANCE for board MQTT / handle");
        }
#endif
#if DEEP_DOG_HTTP_SERVER_ENABLE
        if (http_server_ && !http_server_->IsRunning()) {
            if (!http_server_->Start()) {
                ESP_LOGW(TAG, "HTTP 控制/MJPEG 服务启动失败（可检查端口占用）");
            }
        }
#endif
#if DEEP_DOG_WS_MCP_ENABLE
        {
            static DeepDogWsMcpServer ws_mcp_boot;
            if (ws_mcp_boot.Start()) {
                ESP_LOGI(TAG, "WS MCP on port %u path %s", (unsigned)ws_mcp_boot.Port(), ws_mcp_boot.Path());
                DeepDogWsMcpServer* ws_ptr = &ws_mcp_boot;
                Application::GetInstance().RegisterMcpBroadcastCallback([ws_ptr](const std::string& payload) {
                    ws_ptr->BroadcastMessage(payload);
                });
#if DEEP_DOG_MQTT_ENABLE
                if (board_mqtt_) {
                    board_mqtt_->SetWsMcpEndpoint(ws_mcp_boot.Port(), ws_mcp_boot.Path());
                }
#endif
            } else {
                ESP_LOGW(TAG, "WS MCP 启动失败");
            }
        }
#endif
#if DEEP_DOG_FACE_AI_DEFAULT_ENABLED
        if (!DeepDogFaceAiRuntimeStart()) {
            ESP_LOGW(TAG, "人脸 runtime 未启动（静默识别不可用）");
        }
#else
        ESP_LOGI(TAG, "Face AI off at boot (face/cmd enabled:true to start)");
#endif
#endif
#if DEEP_DOG_BOOT_DEFER_HEAVY_UNTIL_ACTIVATION
        ESP_LOGI(TAG, "Face/hub/MQTT/HTTP deferred until xiaozhi activation done");
#else
#if DEEP_DOG_VISION_HUB_ENABLE
        if (vision_hub_ && !vision_hub_->IsRunning()) {
            if (!vision_hub_->Start()) {
                ESP_LOGW(TAG, "VisionFrameHub 启动失败");
            }
        }
#endif
#endif
    }

    void OnActivationComplete() override {
#if DEEP_DOG_BOOT_DEFER_HEAVY_UNTIL_ACTIVATION
        xTaskCreate(
            [](void* arg) {
                vTaskDelay(pdMS_TO_TICKS(300));
                static_cast<DeepDog*>(arg)->StartPostActivationServices();
                vTaskDelete(nullptr);
            },
            "dog_post_act", 4096, this, 1, nullptr);
#else
        StartPostActivationServices();
#endif
    }
};

DECLARE_BOARD(DeepDog);
