# 11 · handle（手柄）

| 项 | 内容 |
|----|------|
| module_id | `handle` |
| capabilities | `handle` |
| 路由建议 | `/device/:deviceId/modules/handle` |
| 契约 | **扩展 ready**；固件 wifi 源已实现；板载 BT planned |
| YAML | `handle/status`、`handle/input`、`handle/cmd`、**`handle/keymap`**（I08a） |
| 架构 | [swrs/input/](../../input/) · keymap [I08a](../../input/I08a-keymap-mqtt-contract-draft.md) |

## 入口卡文案

- 标题：手柄  
- 说明：摇杆与按键请求值（板载 Xbox / PC 桥）  

## 详情页目标

展示合并后的 `axes` / `buttons` / `connected` / `source`；可选：

- `handle/cmd`：enable / disable / pair / rumble（板载 BT）/ **output**（PC 桥 DS4 灯震）  
- **keymap（I08a）**：订 retain `handle/keymap`；`set_profile` 切 App 类（灯带 / **云台** / 运控 / 关闭）；每键 **press+hold** `set_keymap` 并 NVS 固化  
- 调试：网页或脚本发 `handle/input`（虚拟摇杆 / PC 桥）

**不含**狗动作名；编译期业务映射见 [I04](../../input/I04-apps-mapping.md)；运行时映射见 [I08a](../../input/I08a-keymap-mqtt-contract-draft.md)；云台详见 [09-gimbal](./09-gimbal.md)。

## Topic

| Topic | 方向 | QoS | retain |
|-------|------|-----|--------|
| `handle/status` | ↑ | 0 | false |
| `handle/input` | ↓ | 0 | false |
| `handle/cmd` | ↓ | 1 | false |
| `handle/keymap` | ↑ | 0 | **true** |

双源：板载 Bluepad32+BTstack+Xbox BLE（`source=bt`）与 PC/网页注入（`source=wifi`）并列；Hub **后到覆盖**。详见 [I01](../../input/I01-architecture.md) · [I02](../../input/I02-source-bluepad32-xbox.md)。

## 样例 JSON

完整三段报文（桥 input / 设备 status / 超时清零）与 Xbox·PS4 异同见：

→ **[input/I05-mqtt-message-examples.md](../../input/I05-mqtt-message-examples.md)**

### status / input（核心字段；同构）

```json
{
  "connected": true,
  "source": "wifi",
  "axes": { "lx": 0.0, "ly": -0.4, "rx": 0.0, "ry": 0.0 },
  "buttons": {
    "a": false, "b": false, "x": false, "y": false,
    "l1": false, "r1": false, "l2": 0.0, "r2": 0.0,
    "start": false, "select": false,
    "ps": false, "l3": false, "r3": false, "touch": false,
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
  "ts": 1710000000
}
```

PC 桥将 `source` 设为 `"wifi"`。axes ∈ [-1,1]（**右/下为正**）；l2/r2 ∈ [0,1]。  
`ps`/`touch`/`dpad_*`/`l3`/`r3` 为可选扩展。  
`touchpad` 可选（I06）：桥 **默认 HID** 上报；`x/y` 主触点兼容；`contacts` 最多 2 指。  
`motion` 可选（I07）：同 HID 路径上报手柄 gyro（dps）/ accel（g）；机体系 **+X 右 / +Y 前 / +Z 上**，平放面朝上 `accel_z ≈ -1 g`；≠ 板载 `imu/status`。  
抽象键位 ↔ PS4/Xbox 见 [input/I01](../../input/I01-architecture.md) · [I03](../../input/I03-source-pc-mqtt-bridge.md)。

### cmd

```json
{ "action": "enable", "ts": 1710000000 }
```

`action` ∈ `enable` \| `disable` \| `pair` \| `rumble` \| `output` \| `set_keymap` \| `get_keymap` \| `reset_keymap` \| `set_profile`。

| action | 说明 |
|--------|------|
| `enable` / `disable` | 门控本地 HandleApps；status 仍可更新；`disable` 优先于 profile |
| `pair` | 仅板载 BT：启动扫描/自动连接 |
| `rumble` | 仅板载 BT：双马达短震（I02） |
| `output` | **PC 桥 DS4**：灯条 + 震动（I09）；设备也可 PUBLISH 作告警 |
| `set_keymap` / `get_keymap` / `reset_keymap` | 运行时按键→灯效表（I08a）；写/读/恢复；可选 NVS |
| `set_profile` | 切换 App 类剖面：`led_demo`（灯带 keymap，dog 整停）/ `dog` / `off` |

#### rumble 样例（板载 BT）

```json
{ "action": "rumble", "duration_ms": 250, "weak": 128, "strong": 64, "ts": 1710000000 }
```

#### output 样例（PC 桥 DS4）

```json
{
  "action": "output",
  "led": { "r": 200, "g": 40, "b": 0 },
  "rumble": { "strong": 0.4, "weak": 0.2 },
  "duration_ms": 200,
  "ts": 1710000000
}
```

以 [YAML](../protocol/deep-dog-mqtt.yml) 为准；板载 rumble 见 [I02](../../input/I02-source-bluepad32-xbox.md)；桥 output 见 [I09](../../input/I09-ds4-output-feedback.md)。

### keymap（I08a · 拍板）

`profile` = **哪一类 HandleApp 驱动执行器**（灯带类 / 运控类 / 关闭），与 I04 App 概念同构。字段、样例、出厂默认见：

→ **[input/I08a-keymap-mqtt-contract-draft.md](../../input/I08a-keymap-mqtt-contract-draft.md)**

```json
{ "action": "set_profile", "profile": "led_demo", "persist": true }
```

```json
{
  "action": "set_keymap",
  "merge": true,
  "persist": true,
  "bindings": {
    "a": { "id": "led.static", "r": 255, "g": 0, "b": 0, "brightness": 64 },
    "b": { "id": "led.static", "r": 0, "g": 0, "b": 255, "brightness": 64 }
  }
}
```

## Steps（前端）

1. 校验 `capabilities.handle`。  
2. 订阅 `handle/status`；可视化摇杆/按键与 `source`。  
3. 可选订 retain `handle/keymap`；展示/编辑 profile 与六键绑定（需 `capabilities.led` 时更完整）。  
4. 可选发 `handle/cmd`（`output` / `rumble` / `set_profile` / `set_keymap`）。  
5. 调试可发 `handle/input`。  
6. unmount 退订 status（及 keymap）。  

## 固件实现

- wifi 源：`HandleEventHub` + Dispatcher + `HandleApp*` 已落地；见 [input/](../../input/)。  
- 板载 BT：[`handle/sources/handle_bt`](../../../handle/sources/handle_bt.h) + Bluepad32/**BTstack**；默认关；启用见 [I02](../../input/I02-source-bluepad32-xbox.md) / [`sources/README`](../../../handle/sources/README.md)。  
- PC 桥：`handle/input`（[I03](../../input/I03-source-pc-mqtt-bridge.md)）。  
- **keymap**：`HandleAppKeyMap` + NVS `h_keymap` + retain `handle/keymap` — **已落地**（契约 [I08a](../../input/I08a-keymap-mqtt-contract-draft.md)；后续 App 绑定表按需扩）。  
- 协议层不绑死狗/臂。

## 前端同步

契约变更后同步：

`/Volumes/MacExtStorage/projects/deep-trace/docs/requirements/features/iot/`

本轮以本仓库 SWRS / YAML 为准。

## 验收

- [x] YAML 含 `handle/input`；与 status 同构  
- [x] 双源与 App 分层写入 [input/](../../input/)  
- [x] YAML 含 keymap cmd + `handle/keymap`（I08a 拍板）  
- [x] 板载 Xbox 可驱动 status / App 快照更新（`source=bt`）  
- [x] keymap：`set_profile` / `set_keymap` + NVS + `profile` 门控（gimbal 已联调；其它 App 后续）  
- [ ] PC 桥 DS4 驱动 status + gimbal jog（合入前联调）  
- [ ] disable 后 App 不执行运控（联调勾选）  
- [ ] 前端无 capability 隐藏入口卡（deep-trace）
