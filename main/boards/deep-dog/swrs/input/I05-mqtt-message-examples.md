# I05 · handle MQTT 报文样例（联调参考）

| 项 | 内容 |
|----|------|
| 用途 | 展开 **手柄→桥→设备→前端** 各跳数据；对照终端日志 |
| 权威契约 | [deep-dog-mqtt.yml](../mqtt/protocol/deep-dog-mqtt.yml) · [11-handle](../mqtt/modules/11-handle.md) |
| 架构 | [I01](./I01-architecture.md) · 桥 [I03](./I03-source-pc-mqtt-bridge.md) |

## 链路总览

前缀：`deepdiary/deep-dog/{device_id}/`（例：`device_id=dev`）

```text
① 手柄 USB/BT HID report（厂商二进制）
       │  OS + SDL/pygame.Joystick（只暴露轴/键/hat）
       ▼
② PC 桥内存快照（Normalize 后的 dict，尚未 MQTT）
       │  PUB QoS0
       ▼
③ …/handle/input          ← 桥 → 设备
       │  Hub 合并
       ▼
④ …/handle/status         ← 设备 → 前端
       │  前端 SUB status only
       ▼
⑤ 网页可视化
```

| 跳 | 是否已写入需求 | 形态 |
|----|----------------|------|
| ① 手柄→OS/HID | **本文件 §0**（说明级；非 JSON） | 二进制 HID report |
| ② 桥内 Normalize | 见脚本 `read_snapshot()` | Python dict |
| ③ `handle/input` | **§1** | JSON |
| ④ `handle/status` | **§2** | JSON（与 input 同构） |
| ⑤ 前端 | 只消费 ④ | — |

桥日志里的 `STATUS …` = **④ 的回传**，不是 ① 的 HID 原始包。

---

## 0. 手柄 → PC 桥（HID / pygame）— 此前易漏写

### 0.1 当前桥实际读到什么

脚本用 **`pygame.joystick.Joystick`**，不是 hidapi 原始 report。对 Mac「PS4 Controller」（`ds4_sdl`）可见：

| API | 内容 |
|-----|------|
| `get_axis(0..5)` | 左/右摇杆 + L2/R2 |
| `get_button(0..15)` | 面键、肩键、Share/PS/Options、L3/R3、D-pad、**Touch 点击** |
| `get_hat` | 本机 PS4 常为 0（D-pad 已在 button） |

**没有** `get_touch_x` / 手指坐标之类 API。触控板在 pygame 层只有：

```text
button 15 == Touch Pad Click   → 映射为 buttons.touch (bool)
```

因此：**「手柄到桥」经 pygame 这一路，原始可读信息里不含手指 XY**；只有点击。

### 0.2 硬件 HID 里有没有 XY？

**有（DualShock 4 的 USB/BT HID report 里带触控板触点）。**  
典型 DS4 USB report 除摇杆/键外，还含 touchpad 区块（触点 active、finger id、12-bit X/Y 等）。  
要用 **hidapi / 直接读 HID**（或厂商库）解析，**pygame.Joystick 不会转出来**。

| 层级 | 触控板 XY | 触控板点击 |
|------|-----------|------------|
| DS4 HID report（硬件→OS） | **有** | 有 |
| pygame.Joystick（桥默认路径） | **无** | 有（btn 15） |
| 桥 `--touchpad-xy`（hidapi 全量读） | **有** | 有（HID 派生） |
| MQTT `handle/input\|status` | 可选 `touchpad` | 可选 `buttons.touch` |
| 前端 | 有 `touchpad` 画触点 | TOUCH 高亮 |

结论：默认路径仍是 pygame（仅点击）；开 `--touchpad-xy` 后走 hidapi，从同一份 report 同时解析按键与 XY。规格见 **[I06-touchpad-xy](./I06-touchpad-xy.md)**。

### 0.3 桥内 Normalize 后的逻辑快照（②，非 MQTT）

与即将 PUB 的 JSON 同结构，见 §1。HID 下标→字段见 [I03](./I03-source-pc-mqtt-bridge.md)。

---

## 1. PC 桥 → 设备：`handle/input`

Topic：`deepdiary/deep-dog/dev/handle/input`  
QoS 0 · retain false · `source` 固定 `"wifi"`

### 静置心跳（约每 150ms）

```json
{
  "connected": true,
  "source": "wifi",
  "axes": { "lx": 0.0, "ly": 0.0, "rx": 0.0, "ry": 0.0 },
  "buttons": {
    "a": false, "b": false, "x": false, "y": false,
    "l1": false, "r1": false,
    "l2": 0.0, "r2": 0.0,
    "start": false, "select": false,
    "ps": false, "l3": false, "r3": false, "touch": false,
    "dpad_up": false, "dpad_down": false, "dpad_left": false, "dpad_right": false
  },
  "ts": 1785364524
}
```

### 活动样例（左杆前推 + 按下 ✕）

```json
{
  "connected": true,
  "source": "wifi",
  "axes": { "lx": 0.0, "ly": -0.82, "rx": 0.12, "ry": 0.0 },
  "buttons": {
    "a": true, "b": false, "x": false, "y": false,
    "l1": false, "r1": false,
    "l2": 0.0, "r2": 0.35,
    "start": false, "select": false,
    "ps": false, "l3": false, "r3": false, "touch": false,
    "dpad_up": false, "dpad_down": false, "dpad_left": false, "dpad_right": false
  },
  "ts": 1785364525
}
```

### 触控板 XY 样例（`--touchpad-xy`；可选字段）

```json
{
  "connected": true,
  "source": "wifi",
  "axes": { "lx": 0.0, "ly": 0.0, "rx": 0.0, "ry": 0.0 },
  "buttons": {
    "a": false, "b": false, "x": false, "y": false,
    "l1": false, "r1": false,
    "l2": 0.0, "r2": 0.0,
    "start": false, "select": false,
    "ps": false, "l3": false, "r3": false, "touch": true,
    "dpad_up": false, "dpad_down": false, "dpad_left": false, "dpad_right": false
  },
  "touchpad": {
    "active": true,
    "x": 0.42,
    "y": 0.55,
    "fingers": 2,
    "contacts": [
      { "active": true, "x": 0.42, "y": 0.55 },
      { "active": true, "x": 0.71, "y": 0.48 }
    ]
  },
  "motion": {
    "gyro_x": 0.0,
    "gyro_y": 0.0,
    "gyro_z": 0.0,
    "accel_x": 0.02,
    "accel_y": 0.21,
    "accel_z": -0.98
  },
  "ts": 1785364525
}
```

字段约定：

| 字段 | 范围 / 含义 |
|------|-------------|
| `axes.*` | [-1,1]；**右 / 下为正**；前推左杆 → `ly < 0` |
| `l2`/`r2` | [0,1] |
| `ps`/`l3`/`r3`/`touch`/`dpad_*` | **可选**；桥有则带，固件透传到 status |
| `touchpad` | **可选**（I06）；`x/y` 主触点兼容；`contacts` 最多 2 指 |
| `motion` | **可选**（I07）；机体系 **+X 右 / +Y 前 / +Z 上**；平放面朝上 → `accel_z ≈ -1 g`；gyro dps、accel g；≠ 板载 `imu/status` |

## 2. 设备 → 前端：`handle/status`

Topic：`deepdiary/deep-dog/dev/handle/status`  
与 `handle/input` **同构**（Hub 合并后的当前请求）。前端 **只订本 Topic**。

终端里常见截断形态（与上表同一 JSON，字段可能省略到核心集若旧固件）：

```text
STATUS {"connected":true,"source":"wifi","axes":{"lx":0,"ly":0,"rx":0,"ry":0},"buttons":{...},"ts":...}
```

完整展开示例（新固件含可选键）：

```json
{
  "connected": true,
  "source": "wifi",
  "axes": { "lx": 0.0, "ly": 0.0, "rx": 0.0, "ry": 0.0 },
  "buttons": {
    "a": false, "b": false, "x": false, "y": false,
    "l1": false, "r1": false,
    "l2": 0.0, "r2": 0.0,
    "start": false, "select": false,
    "ps": false, "l3": false, "r3": false, "touch": false,
    "dpad_up": false, "dpad_down": false, "dpad_left": false, "dpad_right": false
  },
  "ts": 1785364524
}
```

超时清零（桥停发 >500ms）时设备会上报：

```json
{
  "connected": false,
  "source": "wifi",
  "axes": { "lx": 0, "ly": 0, "rx": 0, "ry": 0 },
  "buttons": {
    "a": false, "b": false, "x": false, "y": false,
    "l1": false, "r1": false, "l2": 0, "r2": 0,
    "start": false, "select": false,
    "ps": false, "l3": false, "r3": false, "touch": false,
    "dpad_up": false, "dpad_down": false, "dpad_left": false, "dpad_right": false
  },
  "ts": 1785364526
}
```

## 3. 前端侧

- 订阅：仅 `…/handle/status`（及可选 `…/device/info` 读 `capabilities.handle`）
- 不订阅：`handle/input`（避免与设备回环重复）
- 调试区发的虚拟摇杆：走同一 `handle/input`，与 PC 桥等价

## 触控板：有没有坐标？

| | 现状 |
|--|------|
| DS4 **HID report** | 含触点 XY（硬件有） |
| pygame Joystick（**当前桥**） | 仅 Button 15 Click → `buttons.touch`；**无 XY** |
| MQTT / 前端 | 无 `touch_x`/`touch_y`；TOUCH 只高亮点击 |

详见 **§0**。扩展需求见 [I06-touchpad-xy](./I06-touchpad-xy.md)。v0.1 仅点击。

## Xbox vs PS4（一样 / 不一样）

契约一律用 **Xbox 命名的抽象字段**；物理键由边缘 Normalize。

### 一样（映射进同一字段）

| 抽象 | Xbox | PS4 |
|------|------|-----|
| `a`/`b`/`x`/`y` | A/B/X/Y | ✕/○/□/△ |
| `l1`/`r1` | LB/RB | L1/R1 |
| `l2`/`r2` | LT/RT | L2/R2 |
| `select`/`start` | Back/Start | Share/Options |
| `lx/ly`/`rx/ry` | 双摇杆 | 双摇杆 |
| `l3`/`r3` | 摇杆按下 | 摇杆按下 |
| D-pad | 有（常为 hat） | 有（Mac 上为 btn 11–14） |

### 不一样

| 项 | Xbox | PS4 |
|----|------|-----|
| 面键印刷 | 字母 A/B/X/Y | 符号 ✕○□△（位置对应上表） |
| Guide / PS | Guide（Xbox） | PS → 可选 `ps` |
| 触控板 | **无** | 有点击 → `touch`；**无坐标（本栈）** |
| HID 下标 | XInput / pygame Xbox 表 | `ds4_sdl` / `ds4_linux` 不同 |
| 板载直连 | Bluepad32 Xbox BLE（planned） | **不能**无线直连 ESP32；靠 PC 桥 |

前端一页 DualShock 示意皮：Xbox 源时仍亮同一抽象字段，仅皮肤符号仍是 PS 样式（可后续加皮肤切换）。

## 相关命令

```bash
# 桥（会 PUB input，并打印设备 STATUS）
/usr/bin/python3 scripts/deep_dog_handle_bridge.py --via lan --device-id dev --layout ds4_sdl --hz 40

# 仅看原始 HID 下标
/usr/bin/python3 scripts/deep_dog_handle_bridge.py --probe
```
