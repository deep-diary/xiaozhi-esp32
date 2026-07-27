# 08 · led（WS2812）

| 项 | 内容 |
|----|------|
| module_id | `led` |
| capabilities | `led` |
| 路由建议 | `/device/:deviceId/modules/led` |
| 契约 | ready；驱动 + `led_mqtt` |
| YAML | `led/cmd`、`led/status` |
| 参考 | [led/](../../../led/) · thumble MCP 语义 |

## 入口卡文案

- 标题：灯带  
- 说明：WS2812 模式与颜色  

显示条件：`ext_pins.mode === "led"` **且** `capabilities.led === true`（见 [01-device](./01-device.md) / 前端 145）。

## 详情页目标

控制 mode / 颜色 / 亮度 / 动画参数；回读 retain `led/status`。  
**报文只表达灯效物理态**；机器狗/不倒翁业务语义在固件 `led/apps/`，不上报 MQTT。

## Topic

| Topic | 方向 | QoS | retain |
|-------|------|-----|--------|
| `led/status` | ↑ | 0 | true |
| `led/cmd` | ↓ | 1 | false |

触发：cmd 生效后立即发 status；MQTT 重连后再发一次。retain 便于晚进页拿到当前态。

## 字段表 · `led/cmd`（稀疏）

只带要改的键；未带字段沿用当前 status。

| 字段 | 类型 | 说明 |
|------|------|------|
| `mode` | int | `0` 关 / `1` 静态 / `2` 闪烁 / `3` 呼吸 / `4` 滚动 / `5` 系统(应用绑定) |
| `brightness` | int | 0–255 主亮度 |
| `low_brightness` | int | 0–255 低亮度（呼吸/滚动底） |
| `r` / `g` / `b` | int | 0–255 主色 |
| `low_r` / `low_g` / `low_b` | int | 0–255 低色（呼吸/滚动） |
| `interval_ms` | int | 动画间隔 ms |
| `scroll_length` | int | 滚动亮灯数量 |
| `ts` | int | 可选 |

与 thumble MCP 对应：`set_all_color`→1、`blink`→2、`breathe`→3、`scroll`→4、`show_device_state`→5。亮度 MQTT 用 0–255（不用 MCP 的 0–8 level）。

## 字段表 · `led/status`

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | bool | 灯带是否已初始化 |
| `mode` | int | 当前模式 0–5 |
| `brightness` / `low_brightness` | int | 当前亮度 |
| `r` / `g` / `b` | int | 主色 |
| `low_r` / `low_g` / `low_b` | int | 低色 |
| `interval_ms` | int | 动画间隔 |
| `scroll_length` | int | 滚动长度 |
| `led_count` | int | 灯珠数 |
| `ts` | int | Unix 秒 |

以 [YAML](../protocol/deep-dog-mqtt.yml) 为准。

## 样例 JSON

`led/status`（retain）：

```json
{
  "mode": 5,
  "brightness": 32,
  "low_brightness": 4,
  "r": 0, "g": 64, "b": 0,
  "low_r": 0, "low_g": 8, "low_b": 0,
  "interval_ms": 50,
  "scroll_length": 3,
  "led_count": 24,
  "ok": true,
  "ts": 1710000000
}
```

稀疏 `led/cmd` 示例（切静态红）：

```json
{ "mode": 1, "r": 255, "g": 0, "b": 0 }
```

交还应用绑定：

```json
{ "mode": 5 }
```

## 控制权

| 来源 | 行为 |
|------|------|
| `led/cmd` mode 0–4 | 手动覆盖，立即改灯效 |
| MCP `self.led_strip.*` | 同控制层；改完经 `NotifyChanged` 推 `led/status` |
| `led/cmd` mode 5 / `show_device_state` | 交还 `led/apps/led_app_*`（当前狗应用：空闲绿呼吸） |
| 应用绑定 | 仅在 mode=5 时驱动灯效；**不**往 MQTT 塞业务字段 |

MQTT 与 MCP **共用** `LedStripControl` 快照；任一路改灯，retain `led/status` 一致。

## 硬件 / 配置

- `DEEP_DOG_EXT_PIN_MODE=DEEP_DOG_EXT_PIN_LED` → `ext_pins.mode="led"`，DIN 默认 `gpio_a`(38)，`gpio_b`(48) 空闲保留
- `DEEP_DOG_LED_ENABLE=1`（且 `LED_AVAILABLE`）→ `capabilities.led=true`
- 灯珠数：`DEEP_DOG_LED_STRIP_COUNT`（默认 24）

## Steps（前端）

- **Step 1** 校验 `ext_pins.mode==="led"` 且 `capabilities.led`（Hub 卡）；详情页再校验 capability。
- **Step 2** 订阅 `led/status`（retain）。
- **Step 3** UI：模式、RGB、亮度、interval / scroll_length；发稀疏 `led/cmd`。
- **Step 4** unmount 退订。

## 固件实现

分层（均在板内 `led/` + `mqtt/modules/led_mqtt`）：

| 层 | 组件 |
|----|------|
| 驱动 | 公共 `CircularStrip`（RMT/WS2812）：像素/Blink/Breathe/Scroll |
| 控制 | `LedStripControl`：包一层 mode/颜色快照 + 变更回调（**不是**第二套驱动） |
| MQTT | `DeepDogLedMqtt`：`led/cmd` → 控制 API → retain `led/status` |
| MCP | `RegisterLedMcpTools`：`self.led_strip.*` → 同一控制 API → 同步 status |
| 应用 | `led/apps/led_app_dog`：mode=5 时默认绿呼吸 |

`motor/deep_motor_led_state` 仍直接持有 `CircularStrip*`；后续应经控制层（本次未改）。

## 验收

- [ ] `EXT_PIN_MODE=LED` + `LED_ENABLE=1` → `device/info` 含 `mode=led`、`capabilities.led=true`
- [ ] 无 capability / 非 led mode 时 Hub 隐藏灯带卡
- [ ] `led/cmd` mode 1–4 灯效可见；`led/status` retain 一致
- [ ] 语音 MCP `self.led_strip.set_all_color` 等改灯后，`led/status` 同步
- [ ] mode 5 应用绑定可驱动；再发手动 cmd / MCP 可覆盖
- [ ] 文档可供前端做 UI
