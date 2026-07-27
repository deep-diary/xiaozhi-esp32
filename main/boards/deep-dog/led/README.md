# LED 灯带（WS2812）

## 分层

```text
CircularStrip (main/led/)     ← 驱动（硬件时序 / 动画）
    ↑
LedStripControl               ← 模式 0–5 / 颜色快照 / 变更回调（非重复驱动）
    ↑
├── led_mqtt                  ← led/cmd + led/status
├── led_mcp                   ← self.led_strip.* 语音
└── apps/led_app_dog          ← 产品状态绑定（mode=5）
```

## 配置

见 [`FEATURE_FLAGS.md`](../FEATURE_FLAGS.md)：`EXT_PIN_MODE=LED` + `LED_ENABLE=1`。  
DIN 默认 GPIO38（`gpio_a`）；MQTT 契约见 [`swrs/mqtt/modules/08-led.md`](../swrs/mqtt/modules/08-led.md)。

## 文件

| 文件 | 说明 |
|------|------|
| `led_init.*` | 创建 strip + control，启动应用绑定 |
| `led_strip_control.*` | 控制层（包 `CircularStrip`，跟踪 mode） |
| `led_mcp.*` | MCP 适配，工具名对齐 thumble |
| `apps/led_app_dog.*` | 机器狗默认空闲绿呼吸 |
| `mqtt/modules/led_mqtt.*` | MQTT 适配 |
