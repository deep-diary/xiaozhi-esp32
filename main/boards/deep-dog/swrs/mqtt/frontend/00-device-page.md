# 设备页 · 入口卡网格（前端）

| 项 | 内容 |
|----|------|
| 读者 | **前端** |
| 路由建议 | `/device/:deviceId` |
| 依赖 | [M01](../M01-board-mqtt-protocol.md)、[infra](../../vision/infra.md)、[YAML](../protocol/deep-dog-mqtt.yml) |

## 信息架构

| 层级 | 职责 |
|------|------|
| **本页** | Device Basic + **模块入口卡片**；**不展示 detail、不做模块控制** |
| **模块详情页** | 点卡进入；见 [`../modules/`](../modules/) |

**默认**：本页除 `device/info`、`device/status` 外，**不订阅**各模块业务 Topic。

## Topic（本页）

| Topic | 方向 | QoS | retain | 用途 |
|-------|------|-----|--------|------|
| `device/info` | ↑ | 0 | true | capabilities、ext_pins、固件、IP、复位原因、电量能力 |
| `device/status` | ↑ | 0 | false | 心跳：RSSI、内存、health（可选） |

前缀：`deepdiary/deep-dog/{device_id}/`。  
Broker（网页）：`wss://mqtt-ws.deep-diary.com/mqtt`。

## 入口卡顺序

1. Device Basic（页头，非卡）  
2. Stream → IMU → **Face** → Touch → Dog → Motor → UART → LED → Gimbal → Servo → Arm → Handle → CAN  

仅当 `capabilities.<module_id> === true` 时渲染对应入口卡。  
另读 `ext_pins.mode`（`none|can|uart|rs485|pwm|io|ad`）决定总线类页面骨架（与 capabilities 互补；`mode` 为真源）。

**Track 不单独出卡**：跟踪 UI 在 [02-stream](../modules/02-stream.md)（`#track`）。  
**Person 不单独出卡**：并入 [04-face](../modules/04-face.md)；`/modules/person` redirect 到 face。

Stream 卡说明可写「推流与人脸叠加」；Face 卡「检测 / 识别 / Immich」；Touch 卡「三键按下 / 长按 / 短按 / 双击」（详情见 [06-touch](../modules/06-touch.md)）。  
Motor 卡：单电机调试（`capabilities.motor && !dog` 或独立电机页）；Dog 卡：四足运控。

## Steps（前端）

- **Step 1** 使用 `device_id` 连接 EMQX（MQTT over WebSocket）。
- **Step 2** 订阅 `…/device/info`（retain）与可选 `…/device/status`。
- **Step 3** 渲染 Device Basic：`device_id` / `firmware` / `ip` / `http_port` / `ext_pins.mode`；有 status 时附加 `rssi`、内存摘要、`health.ok`。电量仅当 `power.supported===true`。可链 [01-device](../modules/01-device.md)。
- **Step 4** 读 `capabilities` + `ext_pins`，过滤模块列表（**忽略独立 person 卡**），渲染入口卡。
- **Step 5** 点击 → `navigate(/device/:deviceId/modules/:moduleId)`。
- **Step 6** 离页退订；不在此页管理业务 Topic。

## 入口卡禁止 / 允许

| 禁止 | 允许 |
|------|------|
| 完整字段表、控制按钮组、高频图表 | 模块标题、图标、一句话说明 |
| 订阅 `stream/*`、`face/*` 等业务 Topic | 仅用本页已有的 `device/*` |

## 验收

- [ ] 无 capability 的模块不出卡
- [ ] 无独立 Track / Person 卡
- [ ] 点卡进入对应详情页
- [ ] 本页无模块 cmd 按钮、无模块 detail 面板
