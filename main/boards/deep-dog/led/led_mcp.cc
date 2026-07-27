#include "led/led_mcp.h"

#include "led/led_strip_control.h"
#include "led/apps/led_app_dog.h"
#include "led/led_config.h"
#include "config.h"
#include "mcp_server.h"
#include "settings.h"

#include <esp_log.h>

#define TAG "dog_led_mcp"

#if DEEP_DOG_LED_ENABLE

namespace {

int LevelToBrightness(int level) {
    if (level < 0) {
        level = 0;
    }
    if (level > 8) {
        level = 8;
    }
    return (1 << level) - 1;
}

StripColor RGB(int r, int g, int b) {
    return {static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b)};
}

}  // namespace

void RegisterLedMcpTools(McpServer& mcp_server, LedStripControl* control) {
    if (!control || !control->ok()) {
        ESP_LOGW(TAG, "skip MCP tools (no strip)");
        return;
    }

    Settings settings("led_strip");
    static int brightness_level = settings.GetInt("brightness", 4);
    control->SetBrightness(static_cast<uint8_t>(LevelToBrightness(brightness_level)),
                           control->GetStatus().low_brightness);

    mcp_server.AddTool(
        "self.led_strip.get_brightness", "Get the brightness of the led strip (0-8)", PropertyList(),
        [](const PropertyList&) -> ReturnValue { return brightness_level; });

    mcp_server.AddTool(
        "self.led_strip.set_brightness", "Set the brightness of the led strip (0-8)",
        PropertyList({Property("level", kPropertyTypeInteger, 0, 8)}),
        [control](const PropertyList& properties) -> ReturnValue {
            int level = properties["level"].value<int>();
            brightness_level = level;
            const uint8_t b = static_cast<uint8_t>(LevelToBrightness(level));
            const uint8_t lb = control->GetStatus().low_brightness;
            ESP_LOGI(TAG, "set_brightness level=%d -> %u", level, (unsigned)b);
            control->SetBrightness(b, lb);
            Settings s("led_strip", true);
            s.SetInt("brightness", level);
            return true;
        });

    const int max_index = control->led_count() > 0 ? control->led_count() - 1 : 0;
    mcp_server.AddTool(
        "self.led_strip.set_single_color", "Set the color of a single led.",
        PropertyList({Property("index", kPropertyTypeInteger, 0, max_index),
                      Property("red", kPropertyTypeInteger, 0, 255),
                      Property("green", kPropertyTypeInteger, 0, 255),
                      Property("blue", kPropertyTypeInteger, 0, 255)}),
        [control](const PropertyList& properties) -> ReturnValue {
            int index = properties["index"].value<int>();
            auto color = RGB(properties["red"].value<int>(), properties["green"].value<int>(),
                             properties["blue"].value<int>());
            ESP_LOGI(TAG, "set_single_color i=%d rgb=%d,%d,%d", index, color.red, color.green,
                     color.blue);
            if (control->strip()) {
                control->strip()->SetSingleColor(static_cast<uint8_t>(index), color);
            }
            control->UpdateState(DEEP_DOG_LED_MODE_STATIC, color);
            return true;
        });

    mcp_server.AddTool(
        "self.led_strip.set_all_color", "Set the color of all leds.",
        PropertyList({Property("red", kPropertyTypeInteger, 0, 255),
                      Property("green", kPropertyTypeInteger, 0, 255),
                      Property("blue", kPropertyTypeInteger, 0, 255)}),
        [control](const PropertyList& properties) -> ReturnValue {
            auto color = RGB(properties["red"].value<int>(), properties["green"].value<int>(),
                             properties["blue"].value<int>());
            ESP_LOGI(TAG, "set_all_color rgb=%d,%d,%d", color.red, color.green, color.blue);
            control->ApplyStatic(color);
            return true;
        });

    mcp_server.AddTool(
        "self.led_strip.blink", "Blink the led strip. (闪烁)",
        PropertyList({Property("red", kPropertyTypeInteger, 0, 255),
                      Property("green", kPropertyTypeInteger, 0, 255),
                      Property("blue", kPropertyTypeInteger, 0, 255),
                      Property("interval", kPropertyTypeInteger, 0, 1000)}),
        [control](const PropertyList& properties) -> ReturnValue {
            auto color = RGB(properties["red"].value<int>(), properties["green"].value<int>(),
                             properties["blue"].value<int>());
            int interval = properties["interval"].value<int>();
            ESP_LOGI(TAG, "blink rgb=%d,%d,%d interval=%d", color.red, color.green, color.blue,
                     interval);
            control->ApplyBlink(color, interval);
            return true;
        });

    const int max_len = control->led_count() > 0 ? control->led_count() : 1;
    mcp_server.AddTool(
        "self.led_strip.scroll", "Scroll the led strip. (跑马灯)",
        PropertyList({Property("red", kPropertyTypeInteger, 0, 255),
                      Property("green", kPropertyTypeInteger, 0, 255),
                      Property("blue", kPropertyTypeInteger, 0, 255),
                      Property("length", kPropertyTypeInteger, 1, max_len),
                      Property("interval", kPropertyTypeInteger, 0, 100)}),
        [control](const PropertyList& properties) -> ReturnValue {
            auto high = RGB(properties["red"].value<int>(), properties["green"].value<int>(),
                            properties["blue"].value<int>());
            StripColor low = {4, 4, 4};
            int length = properties["length"].value<int>();
            int interval = properties["interval"].value<int>();
            ESP_LOGI(TAG, "scroll rgb=%d,%d,%d len=%d interval=%d", high.red, high.green, high.blue,
                     length, interval);
            control->ApplyScroll(low, high, length, interval);
            return true;
        });

    mcp_server.AddTool(
        "self.led_strip.breathe", "Breathe the led strip. (呼吸灯)",
        PropertyList({Property("low_red", kPropertyTypeInteger, 0, 0, 255),
                      Property("low_green", kPropertyTypeInteger, 0, 0, 255),
                      Property("low_blue", kPropertyTypeInteger, 0, 0, 255),
                      Property("high_red", kPropertyTypeInteger, 255, 0, 255),
                      Property("high_green", kPropertyTypeInteger, 0, 0, 255),
                      Property("high_blue", kPropertyTypeInteger, 0, 0, 255),
                      Property("interval", kPropertyTypeInteger, 50, 10, 100)}),
        [control](const PropertyList& properties) -> ReturnValue {
            auto low = RGB(properties["low_red"].value<int>(), properties["low_green"].value<int>(),
                           properties["low_blue"].value<int>());
            auto high =
                RGB(properties["high_red"].value<int>(), properties["high_green"].value<int>(),
                    properties["high_blue"].value<int>());
            int interval = properties["interval"].value<int>();
            ESP_LOGI(TAG, "breathe low=%d,%d,%d high=%d,%d,%d interval=%d", low.red, low.green,
                     low.blue, high.red, high.green, high.blue, interval);
            control->ApplyBreathe(low, high, interval);
            return true;
        });

    mcp_server.AddTool(
        "self.led_strip.show_device_state",
        "Set led strip to show device/app state. (显示设备状态 / 交还应用绑定)", PropertyList(),
        [control](const PropertyList&) -> ReturnValue {
            ESP_LOGI(TAG, "show_device_state -> app bind (mode=5)");
            LedAppDogOnEnterSystem(control);
            return true;
        });

    ESP_LOGI(TAG, "registered self.led_strip.* MCP tools");
}

#else

void RegisterLedMcpTools(McpServer&, LedStripControl*) {}

#endif
