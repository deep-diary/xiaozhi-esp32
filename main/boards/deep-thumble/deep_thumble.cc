#include "wifi_board.h"
#include "codecs/es8311_audio_codec.h"
#include "display/lcd_display.h"
#include "display/display.h"
#include "display/lvgl_display/lvgl_display.h"
#include "display/lvgl_display/lvgl_image.h"
#include "application.h"
#include "device_state.h"
#include "assets/lang_config.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "settings.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <cstring>
#include <memory>

#include <esp_heap_caps.h>

#include "camera.h"
#include "esp32_camera.h"
#include "led/circular_strip.h"
#include "led/led_control.h"
#include "sensor/imu_sensor.h"

#include "app_ai.hpp"

#include "i2c_bus.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#define TAG "deep_thumble"

// 双队列：相机任务与 MCP take_photo 共用 Capture/GetLastFrame，用互斥锁串行化
class CameraCaptureLockWrapper : public Camera {
public:
    CameraCaptureLockWrapper(Camera* inner, SemaphoreHandle_t mutex) : inner_(inner), mutex_(mutex) {}
    void SetExplainUrl(const std::string& url, const std::string& token) override {
        inner_->SetExplainUrl(url, token);
    }
    bool Capture() override {
        if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) return false;
        bool ok = inner_->Capture();
        xSemaphoreGive(mutex_);
        return ok;
    }
    void SetCapturePreviewEnabled(bool enabled) override { inner_->SetCapturePreviewEnabled(enabled); }
    bool SetHMirror(bool enabled) override { return inner_->SetHMirror(enabled); }
    bool SetVFlip(bool enabled) override { return inner_->SetVFlip(enabled); }
    std::string Explain(const std::string& question) override { return inner_->Explain(question); }
    bool GetLastFrame(CameraFrame* out) override {
        if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) return false;
        bool ok = inner_->GetLastFrame(out);
        xSemaphoreGive(mutex_);
        return ok;
    }
private:
    Camera* inner_;
    SemaphoreHandle_t mutex_;
};

class DeepThumbleEs8311AudioCodec : public Es8311AudioCodec {
private:    

public:
    DeepThumbleEs8311AudioCodec(void* i2c_master_handle, i2c_port_t i2c_port, int input_sample_rate, int output_sample_rate,
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

class DeepThumble : public WifiBoard {
private:
    // 通过 i2c_bus 组件创建的共享 I2C 总线句柄
    i2c_bus_handle_t shared_i2c_bus_handle_ = nullptr;
    // 从共享总线句柄派生出的 IDF 原生 master bus 句柄，供摄像头 / Codec 使用
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    Button boot_button_;
    Display* display_;
    Esp32Camera* camera_;
    CircularStrip* led_strip_;
    LedStripControl* led_control_;
    ImuSensor* imu_sensor_;
    TaskHandle_t user_main_loop_task_handle_ = nullptr;

    SemaphoreHandle_t camera_capture_mutex_ = nullptr;
    Camera* camera_proxy_ = nullptr;

    // ==================== 用户主循环调度相关 ====================
    // 基础周期 10ms
    static constexpr int MAIN_LOOP_BASE_DELAY_MS = 10;

    // 周期倍数（实际周期 = 倍数 × MAIN_LOOP_BASE_DELAY_MS）
    static constexpr int CYCLE_10MS  = 1;   // 10ms
    static constexpr int CYCLE_500MS = 50;  // 500ms

    void InitializeI2c() {
        // 使用 i2c_bus 组件创建共享 I2C 总线，并获得内部的 master bus 句柄
        i2c_config_t i2c_cfg = {
            .mode = I2C_MODE_MASTER,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .sda_pullup_en = true,
            .scl_pullup_en = true,
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
        i2c_bus_ = i2c_bus_get_internal_bus_handle(shared_i2c_bus_handle_);
#else
#error "DeepThumble board requires i2c_bus_get_internal_bus_handle() support"
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
        
        camera_ = new Esp32Camera(video_config);

        Settings settings("deep-thumble", false);
        // 只翻转摄像头输出（不翻转整块屏幕），与摄像头安装方向一致
        bool camera_flipped = static_cast<bool>(settings.GetInt("camera-flipped", 1));
        ESP_LOGI(TAG, "Camera Flipped: %d", camera_flipped);
        camera_->SetHMirror(camera_flipped);
        camera_->SetVFlip(camera_flipped);
    }

    void InitializeLedStrip() {
        ESP_LOGI(TAG, "初始化WS2812灯带...GPIO=%d, LED数量=%d", 
                 WS2812_STRIP_GPIO, WS2812_LED_COUNT);
        led_strip_ = new CircularStrip(WS2812_STRIP_GPIO, WS2812_LED_COUNT);
        ESP_LOGI(TAG, "WS2812灯带初始化完成");
    }

    void InitializeImuSensor() {
        // 使用与音频 Codec / 摄像头共享的 I2C 总线（i2c_bus 组件）初始化 BMI270
        imu_sensor_ = new ImuSensor(shared_i2c_bus_handle_);
        if (imu_sensor_->Initialize()) {
            ESP_LOGI(TAG, "IMU 传感器 (BMI270) 初始化完成");
        } else {
            ESP_LOGW(TAG, "IMU 传感器 (BMI270) 初始化失败，后续读取将被忽略");
        }
    }

    void InitializeFaceRecognition() {
        if (!camera_ || !display_) {
            ESP_LOGW(TAG, "app_ai skipped: camera or display not ready");
            return;
        }
        if (camera_proxy_) {
            return;  // 已初始化过
        }
        camera_capture_mutex_ = xSemaphoreCreateMutex();
        if (!camera_capture_mutex_) {
            ESP_LOGE(TAG, "Camera capture mutex create failed");
            return;
        }
        camera_proxy_ = new CameraCaptureLockWrapper(camera_, camera_capture_mutex_);
        app_ai::Start(camera_proxy_, display_);
    }

    static void DelayedFaceInitTask(void* pv) {
        auto* self = static_cast<DeepThumble*>(pv);
        if (!self) {
            vTaskDelete(nullptr);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(FACE_RECOGNITION_DELAYED_START_MS));
        ESP_LOGI(TAG, "Delayed face init: starting after %d ms", (int)FACE_RECOGNITION_DELAYED_START_MS);
        self->InitializeFaceRecognition();
        vTaskDelete(nullptr);
    }

    // 用户主循环任务：10ms 基础周期调度（阶段三）
    static void UserMainLoopTask(void* pvParameters) {
        auto* self = static_cast<DeepThumble*>(pvParameters);
        if (!self) {
            vTaskDelete(nullptr);
        }

        int32_t cycle_counter = 0;
        ImuRawData imu_data{};

        ESP_LOGI(TAG, "UserMainLoopTask started (base period %d ms).", MAIN_LOOP_BASE_DELAY_MS);

        while (true) {
            vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_BASE_DELAY_MS));
            cycle_counter++;

            // ===== 10ms 周期任务：IMU 采集 =====
            if ((cycle_counter % CYCLE_10MS) == 0) {
                if (self->imu_sensor_ && self->imu_sensor_->IsInitialized()) {
                    if (self->imu_sensor_->ReadRawData(&imu_data)) {
                        // 每 100 个 10ms 周期（约 1 秒）打印一次 IMU 数据
                        static int log_divider = 0;
                        log_divider++;
                        if (log_divider >= 100) {
                            log_divider = 0;
                            ESP_LOGD(TAG,
                                     "IMU raw data: accel=(%.2f, %.2f, %.2f) gyro=(%.2f, %.2f, %.2f)",
                                     imu_data.accel_x, imu_data.accel_y, imu_data.accel_z,
                                     imu_data.gyro_x, imu_data.gyro_y, imu_data.gyro_z);
                        }
                    }
                }
            }

            // ===== 500ms 周期任务：预留（当前仅日志占位） =====
            if ((cycle_counter % CYCLE_500MS) == 0) {
                ESP_LOGD(TAG, "UserMainLoop 500ms tick");
            }

            // 防止计数器溢出过大
            if (cycle_counter >= 1000000) {
                cycle_counter = 0;
            }
        }
    }

    void InitializeTools() {
        auto& mcp_server = McpServer::GetInstance();

        // 初始化LED灯带控制类
        led_control_ = new LedStripControl(led_strip_, mcp_server);
        ESP_LOGI(TAG, "LED灯带控制类初始化完成");

        mcp_server.AddTool("self.camera.set_camera_flipped", "翻转摄像头图像方向", PropertyList(), [this](const PropertyList& properties) -> ReturnValue {
            Settings settings("deep-thumble", true);
            // 考虑到部分复刻使用了不可动摄像头的设计，默认启用翻转
            bool flipped = !static_cast<bool>(settings.GetInt("camera-flipped", 1));
            
            camera_->SetHMirror(flipped);
            camera_->SetVFlip(flipped);
            
            settings.SetInt("camera-flipped", flipped ? 1 : 0);
            
            return true;
        });
    }

public:
    DeepThumble() : boot_button_(BOOT_BUTTON_GPIO),
                    led_strip_(nullptr),
                    led_control_(nullptr),
                    imu_sensor_(nullptr) {
        InitializeI2c();
        InitializeSpi();
        InitializeDisplay();
        InitializeButtons();
        InitializeCamera();
        InitializeImuSensor();
        InitializeLedStrip();
        InitializeTools();
        xTaskCreate(DelayedFaceInitTask, "face_delayed", 2048, this, 3, nullptr);

        // 启动用户主循环任务：10ms 基础周期调度（阶段三）
        if (imu_sensor_ && imu_sensor_->IsInitialized()) {
            BaseType_t ret = xTaskCreate(
                UserMainLoopTask,
                "user_main_loop",
                4096,
                this,
                4,
                &user_main_loop_task_handle_);
            if (ret != pdPASS) {
                ESP_LOGE(TAG, "Failed to create user_main_loop task");
                user_main_loop_task_handle_ = nullptr;
            } else {
                ESP_LOGI(TAG, "user_main_loop task created successfully");
            }
        } else {
            ESP_LOGW(TAG, "IMU not initialized, skip user_main_loop task");
        }

        GetBacklight()->RestoreBrightness();
    }

    virtual AudioCodec* GetAudioCodec() override {
         static DeepThumbleEs8311AudioCodec audio_codec(i2c_bus_, I2C_NUM_0, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
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
        return camera_proxy_ ? camera_proxy_ : camera_;
    }
};

DECLARE_BOARD(DeepThumble);
