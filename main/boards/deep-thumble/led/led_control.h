#ifndef LED_CONTROL_H
#define LED_CONTROL_H

#include "led/circular_strip.h"
#include "mcp_server.h"

class LedStripControl {
private:
    CircularStrip* led_strip_;
    int brightness_level_;  // 亮度等级 (0-8)
    
    // LED状态跟踪
    int current_mode_;  // 0=关闭, 1=静态, 2=闪烁, 3=呼吸, 4=滚动, 5=系统状态
    StripColor current_color_;  // 当前主颜色
    StripColor low_color_;  // 低亮度颜色（用于呼吸灯和滚动）
    int current_interval_ms_;  // 当前动画间隔
    int current_scroll_length_;  // 滚动模式下的亮灯数量
    uint8_t default_brightness_;  // 默认亮度
    uint8_t low_brightness_;  // 低亮度

    int LevelToBrightness(int level) const;  // 将等级转换为实际亮度值
    StripColor RGBToColor(int red, int green, int blue);

public:
    explicit LedStripControl(CircularStrip* led_strip, McpServer& mcp_server);
    
    // 获取当前状态
    int GetCurrentMode() const { return current_mode_; }
    uint8_t GetDefaultBrightness() const { return default_brightness_; }
    uint8_t GetLowBrightness() const { return low_brightness_; }
    StripColor GetCurrentColor() const { return current_color_; }
    StripColor GetLowColor() const { return low_color_; }
    int GetCurrentInterval() const { return current_interval_ms_; }
    int GetCurrentScrollLength() const { return current_scroll_length_; }
    
    // 手动更新状态（用于外部直接调用led_strip_方法后同步状态）
    void UpdateState(int mode, StripColor color, int interval_ms = 500, int scroll_length = 3);
    void UpdateState(int mode, StripColor main_color, StripColor low_color, int interval_ms = 500, int scroll_length = 3);
    void UpdateBrightness(uint8_t default_brightness, uint8_t low_brightness);
}; 

#endif // LED_STRIP_CONTROL_H
