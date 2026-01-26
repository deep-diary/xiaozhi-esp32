# LED控制模块 (LED Control Module)

## 功能概述

本模块提供了WS2812可编程LED灯带的控制功能，包括：
- 单个LED颜色控制
- 灯带整体效果
- 动画效果（呼吸、流水、彩虹等）
- MCP服务器接口

## 文件说明

### 核心文件

- **led_control.cc/h**: LED灯带控制类
  - 基于CircularStrip实现
  - 提供多种预设效果
  - 暴露MCP控制接口

## 主要功能

### 1. LED初始化
```cpp
// 创建LED灯带对象（GPIO, LED数量）
CircularStrip* led_strip = new CircularStrip(WS2812_STRIP_GPIO, WS2812_LED_COUNT);

// 创建LED控制对象
auto& mcp_server = McpServer::GetInstance();
LedStripControl* led_control = new LedStripControl(led_strip, mcp_server);
```

### 2. 基本颜色控制
```cpp
// 设置单个LED颜色（LED索引, R, G, B）
led_strip->SetPixel(0, 255, 0, 0);  // 第一个LED设为红色

// 设置所有LED相同颜色
led_strip->Fill(0, 255, 0);  // 全部设为绿色

// 清空所有LED
led_strip->Clear();

// 刷新显示
led_strip->Refresh();
```

### 3. 动画效果
```cpp
// 呼吸效果（颜色, 周期ms）
led_strip->Breathe(255, 0, 255, 2000);  // 紫色呼吸，周期2秒

// 流水效果（颜色, 速度ms）
led_strip->Flow(0, 255, 255, 100);  // 青色流水，速度100ms

// 彩虹效果（速度ms）
led_strip->Rainbow(50);  // 彩虹效果，速度50ms

// 旋转效果（颜色, LED数量, 速度ms）
led_strip->Rotate(255, 255, 0, 3, 100);  // 黄色旋转，3个LED，速度100ms
```

### 4. MCP工具使用
通过MCP服务器可以使用以下工具：
- `led_set_color`: 设置LED颜色
- `led_set_brightness`: 设置亮度
- `led_breathe`: 呼吸效果
- `led_flow`: 流水效果
- `led_rainbow`: 彩虹效果
- `led_clear`: 清空所有LED

## 使用示例

### 基本控制示例
```cpp
// 1. 初始化LED灯带
CircularStrip* led_strip = new CircularStrip(8, 12);  // GPIO8, 12个LED

// 2. 设置颜色
led_strip->Fill(255, 0, 0);    // 全红
led_strip->Refresh();

// 3. 延时后切换颜色
vTaskDelay(pdMS_TO_TICKS(1000));
led_strip->Fill(0, 255, 0);    // 全绿
led_strip->Refresh();
```

### 动态效果示例
```cpp
// 彩虹流动效果
while (true) {
    led_strip->Rainbow(50);
    vTaskDelay(pdMS_TO_TICKS(50));
}

// 红色呼吸效果
while (true) {
    led_strip->Breathe(255, 0, 0, 2000);
    vTaskDelay(pdMS_TO_TICKS(20));
}
```

### 自定义效果示例
```cpp
// 逐个点亮LED
for (int i = 0; i < LED_COUNT; i++) {
    led_strip->Clear();
    led_strip->SetPixel(i, 255, 255, 255);
    led_strip->Refresh();
    vTaskDelay(pdMS_TO_TICKS(100));
}
```

### 状态指示示例
```cpp
// 根据状态设置颜色
void SetStatusLED(SystemState state) {
    switch (state) {
        case STATE_IDLE:
            led_strip->Fill(0, 255, 0);  // 绿色 - 空闲
            break;
        case STATE_BUSY:
            led_strip->Fill(255, 255, 0);  // 黄色 - 忙碌
            break;
        case STATE_ERROR:
            led_strip->Fill(255, 0, 0);  // 红色 - 错误
            break;
    }
    led_strip->Refresh();
}
```

## 颜色参考

### 基本颜色
```cpp
// 红色
led_strip->Fill(255, 0, 0);

// 绿色
led_strip->Fill(0, 255, 0);

// 蓝色
led_strip->Fill(0, 0, 255);

// 白色
led_strip->Fill(255, 255, 255);

// 黄色
led_strip->Fill(255, 255, 0);

// 青色
led_strip->Fill(0, 255, 255);

// 紫色
led_strip->Fill(255, 0, 255);
```

### 常用状态颜色建议
- **空闲**: 绿色 (0, 255, 0)
- **运行中**: 蓝色 (0, 0, 255)
- **警告**: 黄色 (255, 255, 0)
- **错误**: 红色 (255, 0, 0)
- **成功**: 绿色呼吸
- **等待**: 蓝色流动

## 硬件连接

### WS2812灯带接线
```
WS2812      ESP32
------      -------
VCC   →     5V (外部供电)
GND   →     GND
DIN   →     GPIO_X
```

**重要提示**:
- WS2812需要5V供电
- 大量LED时需要外部电源（每个LED最大60mA）
- 数据线建议添加100-500Ω电阻
- 电源端建议添加1000μF电容

## 性能参数

- **最大LED数量**: 理论上无限制，实际受内存限制
- **刷新频率**: 最高约400Hz（取决于LED数量）
- **颜色深度**: 24位真彩色（每通道8位）
- **亮度范围**: 0-255

## 依赖关系

- **CircularStrip类**: 底层LED驱动（位于`led/circular_strip.h`）
- **MCP服务器**: 对外控制接口
- **ESP-IDF RMT驱动**: 用于WS2812时序控制

## 注意事项

1. **电源要求**:
   - 单个LED最大功耗：60mA（白色全亮）
   - 计算公式：总电流 = LED数量 × 60mA
   - 12个LED建议使用至少1A的5V电源

2. **信号完整性**:
   - 数据线不宜过长（建议<3米）
   - 长距离传输建议使用信号放大器
   - 首个LED前添加串联电阻（100-500Ω）

3. **性能优化**:
   - 减少不必要的Refresh()调用
   - 批量设置后统一刷新
   - 降低亮度可减少功耗和发热

4. **颜色校正**:
   - 不同批次LED可能存在色差
   - 可通过调整RGB比例进行校正

5. **使用建议**:
   - 避免长时间高亮度使用
   - 注意散热，避免LED过热
   - 断电前建议先关闭LED

## 状态显示应用

### 电机状态显示
LED灯带可用于显示电机状态（已集成在电机模块）：
- **绿色**: 电机使能且空闲
- **蓝色**: 电机运动中
- **黄色**: 电机接近目标
- **红色**: 电机故障

### 机械臂状态显示
LED灯带可用于显示机械臂状态（已集成在机械臂模块）：
- **绿色呼吸**: 空闲状态
- **蓝色流动**: 运动中
- **红色闪烁**: 录制中
- **紫色流动**: 播放中

## 相关模块

本模块被以下模块使用：
- **电机模块** (`../motor/`): 电机状态显示
- **机械臂模块** (`../arm/`): 机械臂状态显示

## 扩展功能

可以基于本模块实现：
- 音乐律动效果
- 环境光感应自动调节
- 渐变色效果
- 自定义动画序列
- 多段显示（不同区域不同效果）

