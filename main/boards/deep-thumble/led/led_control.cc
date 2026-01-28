#include "led_control.h"
#include "settings.h"
#include "mcp_server.h"
#include <esp_log.h>
#include "../config.h"


#define TAG "LedStripControl"


int LedStripControl::LevelToBrightness(int level) const {
    if (level < 0) level = 0;
    if (level > 8) level = 8;
    return (1 << level) - 1;  // 2^n - 1
}

StripColor LedStripControl::RGBToColor(int red, int green, int blue) {
    return {static_cast<uint8_t>(red), static_cast<uint8_t>(green), static_cast<uint8_t>(blue)};
}

LedStripControl::LedStripControl(CircularStrip* led_strip, McpServer& mcp_server) 
    : led_strip_(led_strip)
    , current_mode_(0)  // 默认关闭
    , current_color_({0, 0, 0})
    , low_color_({0, 0, 0})
    , current_interval_ms_(500)
    , current_scroll_length_(3)
    , default_brightness_(LevelToBrightness(4))
    , low_brightness_(4) {
    // 从设置中读取亮度等级
    Settings settings("led_strip");
    brightness_level_ = settings.GetInt("brightness", 4);  // 默认等级4
    default_brightness_ = LevelToBrightness(brightness_level_);
    led_strip_->SetBrightness(default_brightness_, low_brightness_);
    mcp_server.AddTool("self.led_strip.get_brightness",
        "Get the brightness of the led strip (0-8)",
        PropertyList(), [this](const PropertyList& properties) -> ReturnValue {
            return brightness_level_;
        });

    mcp_server.AddTool("self.led_strip.set_brightness",
        "Set the brightness of the led strip (0-8)",
        PropertyList({
            Property("level", kPropertyTypeInteger, 0, 8)
        }), [this](const PropertyList& properties) -> ReturnValue {
            int level = properties["level"].value<int>();
            ESP_LOGI(TAG, "Set LedStrip brightness level to %d", level);
            brightness_level_ = level;
            default_brightness_ = LevelToBrightness(brightness_level_);
            led_strip_->SetBrightness(default_brightness_, low_brightness_);

            // 保存设置
            Settings settings("led_strip", true);
            settings.SetInt("brightness", brightness_level_);

            return true;
        });

    mcp_server.AddTool("self.led_strip.set_single_color", 
        "Set the color of a single led.", 
        PropertyList({
            Property("index", kPropertyTypeInteger, 0, WS2812_LED_COUNT - 1),
            Property("red", kPropertyTypeInteger, 0, 255),
            Property("green", kPropertyTypeInteger, 0, 255),
            Property("blue", kPropertyTypeInteger, 0, 255)
        }), [this](const PropertyList& properties) -> ReturnValue {
            int index = properties["index"].value<int>();
            int red = properties["red"].value<int>();
            int green = properties["green"].value<int>();
            int blue = properties["blue"].value<int>();
            ESP_LOGI(TAG, "Set led strip single color %d to %d, %d, %d",
                index, red, green, blue);
            led_strip_->SetSingleColor(index, RGBToColor(red, green, blue));
            return true;
        });

    mcp_server.AddTool("self.led_strip.set_all_color", 
        "Set the color of all leds.", 
        PropertyList({
            Property("red", kPropertyTypeInteger, 0, 255),
            Property("green", kPropertyTypeInteger, 0, 255),
            Property("blue", kPropertyTypeInteger, 0, 255)
        }), [this](const PropertyList& properties) -> ReturnValue {
            int red = properties["red"].value<int>();
            int green = properties["green"].value<int>();
            int blue = properties["blue"].value<int>();
            ESP_LOGI(TAG, "Set led strip all color to %d, %d, %d",
                red, green, blue);
            StripColor color = RGBToColor(red, green, blue);
            current_mode_ = (red == 0 && green == 0 && blue == 0) ? 0 : 1;
            current_color_ = color;
            led_strip_->SetAllColor(color);
            return true;
        });

    mcp_server.AddTool("self.led_strip.blink", 
        "Blink the led strip. (闪烁)", 
        PropertyList({
            Property("red", kPropertyTypeInteger, 0, 255),
            Property("green", kPropertyTypeInteger, 0, 255),
            Property("blue", kPropertyTypeInteger, 0, 255),
            Property("interval", kPropertyTypeInteger, 0, 1000)
        }), [this](const PropertyList& properties) -> ReturnValue {
            int red = properties["red"].value<int>();
            int green = properties["green"].value<int>();
            int blue = properties["blue"].value<int>();
            int interval = properties["interval"].value<int>();
            ESP_LOGI(TAG, "Blink led strip with color %d, %d, %d, interval %dms",
                red, green, blue, interval);
            StripColor color = RGBToColor(red, green, blue);
            current_mode_ = 2;
            current_color_ = color;
            current_interval_ms_ = interval;
            led_strip_->Blink(color, interval);
            return true;
        });

    mcp_server.AddTool("self.led_strip.scroll", 
        "Scroll the led strip. (跑马灯)", 
        PropertyList({
            Property("red", kPropertyTypeInteger, 0, 255),
            Property("green", kPropertyTypeInteger, 0, 255),
            Property("blue", kPropertyTypeInteger, 0, 255),
            Property("length", kPropertyTypeInteger, 1, WS2812_LED_COUNT),
            Property("interval", kPropertyTypeInteger, 0, 100)
        }), [this](const PropertyList& properties) -> ReturnValue {
            int red = properties["red"].value<int>();
            int green = properties["green"].value<int>();
            int blue = properties["blue"].value<int>();
            int interval = properties["interval"].value<int>();
            int length = properties["length"].value<int>();
            ESP_LOGI(TAG, "Scroll led strip with color %d, %d, %d, length %d, interval %dms",
                red, green, blue, length, interval);
            StripColor low = RGBToColor(4, 4, 4);
            StripColor high = RGBToColor(red, green, blue);
            current_mode_ = 4;
            current_color_ = high;
            low_color_ = low;
            current_interval_ms_ = interval;
            current_scroll_length_ = length;
            led_strip_->Scroll(low, high, length, interval);
            return true;
        });

    mcp_server.AddTool("self.led_strip.breathe", 
        "Breathe the led strip. (呼吸灯)", 
        PropertyList({
            Property("low_red", kPropertyTypeInteger, 0, 0, 255),
            Property("low_green", kPropertyTypeInteger, 0, 0, 255),
            Property("low_blue", kPropertyTypeInteger, 0, 0, 255),
            Property("high_red", kPropertyTypeInteger, 255, 0, 255),
            Property("high_green", kPropertyTypeInteger, 0, 0, 255),
            Property("high_blue", kPropertyTypeInteger, 0, 0, 255),
            Property("interval", kPropertyTypeInteger, 50, 10, 100)
        }), [this](const PropertyList& properties) -> ReturnValue {
            int low_red = properties["low_red"].value<int>();
            int low_green = properties["low_green"].value<int>();
            int low_blue = properties["low_blue"].value<int>();
            int high_red = properties["high_red"].value<int>();
            int high_green = properties["high_green"].value<int>();
            int high_blue = properties["high_blue"].value<int>();
            int interval = properties["interval"].value<int>();
            
            ESP_LOGI(TAG, "Breathe led strip with low color %d, %d, %d, high color %d, %d, %d, interval %dms",
                low_red, low_green, low_blue, high_red, high_green, high_blue, interval);
            StripColor low = RGBToColor(low_red, low_green, low_blue);
            StripColor high = RGBToColor(high_red, high_green, high_blue);
            current_mode_ = 3;
            current_color_ = high;
            low_color_ = low;
            current_interval_ms_ = interval;
            led_strip_->Breathe(low, high, interval);
            return true;
        });

    mcp_server.AddTool("self.led_strip.show_device_state", 
        "Set led strip to show device state. (显示设备状态)", 
        PropertyList(), [this](const PropertyList& properties) -> ReturnValue {
            ESP_LOGI(TAG, "Set led strip to show device state");
            current_mode_ = 5;  // 系统状态模式
            led_strip_->OnStateChanged();
            return true;
        });

}

void LedStripControl::UpdateState(int mode, StripColor color, int interval_ms, int scroll_length) {
    current_mode_ = mode;
    current_color_ = color;
    current_interval_ms_ = interval_ms;
    current_scroll_length_ = scroll_length;
}

void LedStripControl::UpdateState(int mode, StripColor main_color, StripColor low_color, int interval_ms, int scroll_length) {
    current_mode_ = mode;
    current_color_ = main_color;
    low_color_ = low_color;
    current_interval_ms_ = interval_ms;
    current_scroll_length_ = scroll_length;
}

void LedStripControl::UpdateBrightness(uint8_t default_brightness, uint8_t low_brightness) {
    default_brightness_ = default_brightness;
    low_brightness_ = low_brightness;
    // 注意：这里不更新brightness_level_，因为它需要反向转换
    // 如果需要，可以添加一个方法来根据brightness值计算level
}
