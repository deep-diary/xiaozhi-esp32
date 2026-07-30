# 11 · handle（手柄）

| 项 | 内容 |
|----|------|
| module_id | `handle` |
| capabilities | `handle` |
| 路由建议 | `/device/:deviceId/modules/handle` |
| 契约 | **扩展 ready**；固件 planned（双源 + Hub/App） |
| YAML | `handle/status`、`handle/input`、`handle/cmd` |
| 架构 | [swrs/input/](../../input/) |

## 入口卡文案

- 标题：手柄  
- 说明：摇杆与按键请求值（板载 Xbox / PC 桥）  

## 详情页目标

展示合并后的 `axes` / `buttons` / `connected` / `source`；可选：

- `handle/cmd`：enable / disable / pair  
- 调试：网页或脚本发 `handle/input`（虚拟摇杆 / PC 桥）

**不含**狗动作名；业务映射见 [I04](../../input/I04-apps-mapping.md)。

## Topic

| Topic | 方向 | QoS | retain |
|-------|------|-----|--------|
| `handle/status` | ↑ | 0 | false |
| `handle/input` | ↓ | 0 | false |
| `handle/cmd` | ↓ | 1 | false |

双源：板载 Bluepad32+Xbox（`source=bt`）与 PC/网页注入（`source=wifi`）并列；Hub **后到覆盖**。详见 [I01](../../input/I01-architecture.md)。

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
`touchpad` 可选（I06）：`--touchpad-xy` 时上报；`x/y` 主触点兼容；`contacts` 最多 2 指。  
`motion` 可选（I07）：同 HID 路径上报手柄 gyro（dps）/ accel（g）；机体系 **+X 右 / +Y 前 / +Z 上**，平放面朝上 `accel_z ≈ -1 g`；≠ 板载 `imu/status`。  
抽象键位 ↔ PS4/Xbox 见 [input/I01](../../input/I01-architecture.md) · [I03](../../input/I03-source-pc-mqtt-bridge.md)。

### cmd

```json
{ "action": "enable", "ts": 1710000000 }
```

`action` ∈ `enable` \| `disable` \| `pair`（`pair` 仅板载 BT）。

以 [YAML](../protocol/deep-dog-mqtt.yml) 为准。

## Steps（前端）

1. 校验 `capabilities.handle`。  
2. 订阅 `handle/status`；可视化摇杆/按键与 `source`。  
3. 可选发 `handle/cmd`；调试可发 `handle/input`。  
4. unmount 退订 status。  

## 固件实现

- planned：`HandleEventHub` + Dispatcher + `HandleApp*`；见 [input/](../../input/)。  
- 板载 BT 前置：扩 OTA 分区 + NimBLE/Bluepad32（[I02](../../input/I02-source-bluepad32-xbox.md)）。  
- PC 桥可不改 Flash 先通 `handle/input`（[I03](../../input/I03-source-pc-mqtt-bridge.md)）。  
- 协议层不绑死狗/臂。

## 前端同步

契约变更后同步：

`/Volumes/MacExtStorage/projects/deep-trace/docs/requirements/features/iot/`

本轮以本仓库 SWRS / YAML 为准。

## 验收

- [x] YAML 含 `handle/input`；与 status 同构  
- [x] 双源与 App 分层写入 [input/](../../input/)  
- [ ] 无 capability 隐藏入口卡（固件开 `handle` 后）  
- [ ] 板载 Xbox 或 PC 桥任一源可驱动 status 更新  
- [ ] disable 后 App 不执行运控  
