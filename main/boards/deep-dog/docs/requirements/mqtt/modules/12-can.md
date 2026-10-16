# 12 · can（CAN 透传）

| 项 | 内容 |
|----|------|
| module_id | `can` |
| capabilities | `can` |
| 路由建议 | `/device/:deviceId/modules/can` |
| 契约 | ready；固件本轮落地（MOT-02） |
| 需求 | [docs/requirements/motor/02](../../motor/02-can-mqtt.md) |
| YAML | `cmd/can`、`status/can`、`status/can/frames`、`cmd/can/tx` |
| 硬件 | GPIO38/48 TWAI（sparkbot 原 UART 脚） |
| 网页对齐 | deep-trace `modules/can/80-can-web-tunnel`（`docs/requirements/features/iot/modules/can/`） |

## 入口卡文案

- 标题：CAN  
- 说明：总线帧透传监视  

## 详情页目标

开关 tunnel、看 `status/can`、滚动显示 `status/can/frames`。

> **联调期默认（调试剖面）**：固件默认 `tunnel=true`、`allow_tx=true`、`ext_only=false`，
> 网页零配置即可看到全部帧（含 11 位标准帧）/ 注帧。
> `allow_tx=true` 意味着**网页可向电机总线注入原始帧**，仅限台架联调；正式 / 量产版本必须改回
> `allow_tx=false`、`ext_only=true`（`can_mqtt.h` 成员初值）。另有 **2 s 心跳帧**默认开启，见下节。

## Topic

| Topic | 方向 | QoS | retain |
|-------|------|-----|--------|
| `status/can` | ↑ | 0 | true |
| `status/can/frames` | ↑ | 0 | false |
| `cmd/can` | ↓ | 1 | false |
| `cmd/can/tx` | ↓ | 1 | false | 仅 `allow_tx=true` |

## 心跳帧（联调自检）

固件默认每 **2000 ms** 向透传管线注入 **1 条模拟 RX 帧**（**不写总线**，仅走
`DeepDogCanNotifyFrame → OnFrame → status/can/frames`），用于把「MQTT / 网页显示问题」与
「总线真没报文」区分开：

| 项 | 值 |
|----|----|
| 方向 | `dir: "rx"` |
| ID | `0x1F000000`（29 位扩展帧，保留段，与电机协议不冲突） |
| DLC | 8 |
| data | `seq(4B 大端) + uptime_s(4B 大端)` |
| 周期 | `DEEP_DOG_CAN_HEARTBEAT_MS`（`can/can_config.h`，默认 `2000`；`0` = 关闭） |

判读：

- 网页能看到 `id_hex = 0x1F000000` → **透传链路正常**；真实帧收不到则应查总线侧
  （终端电阻 / 波特率 / `status/can.bus_state` / `rx_err` / `bus_err`）。
- 连心跳也看不到 → 问题在透传链路（`tunnel=false`、未订阅 `status/can/frames`、MQTT 未连等）。

心跳与真实帧共用 `status/can/frames`，受同一 `max_hz` / `batch_max` 节流与 `tunnel` 开关约束。

## 样例 JSON

**cmd（开透传 + 节流）**

```json
{
  "tunnel": true,
  "mirror_tx": true,
  "allow_tx": false,
  "max_hz": 50,
  "batch_max": 32,
  "ext_only": true,
  "ts": 1710000000
}
```

**frames**

```json
{
  "frames": [
    {
      "dir": "rx",
      "id": 305419896,
      "id_hex": "0x12345678",
      "ext": true,
      "rtr": false,
      "dlc": 8,
      "data_hex": "0102030405060708",
      "ts_ms": 123456
    }
  ],
  "dropped": 0,
  "ts": 1710000000
}
```

以 [YAML](../protocol/deep-dog-mqtt.yml) 为准。
**联调剖面**默认不过滤（`ext_only=false`，标准帧与扩展帧都上报）；量产须改回 `ext_only=true`，
避免默认全量无过滤透传。

#### 数据点归一化写入（`cmd/can`，扁平字段）

`can` 已纳入[数据点归一化](../../../interface/04-datapoint-registry.md)，下列字段为 **`rw`** 点，
经 `cmd/can` 以**扁平字段**下发（`can` 为单实例模块，键名即点名）：

| 字段 | 类型 | 说明 |
|------|------|------|
| `tunnel` | bool | 透传开关（等效上例 `tunnel`）；固件默认 `true`（联调剖面） |
| `allow_tx` | bool | 是否允许网页注入（危险）；固件默认 `true`（联调剖面），量产须改回 `false` |

```json
{ "tunnel": true }
```

- 表中**未列出**的字段（`mirror_tx` / `max_hz` / `batch_max` / `ext_only`）仍由模块自身 `cmd/can`
  处理保持原语义；registry 未匹配到数据点的字段**静默忽略**；
- 回读取 `status/can`（分层见 [04 §3.3](../../../interface/04-datapoint-registry.md)）；
- 通用契约见 `interop 04 §3.7`。

## Steps（前端）

- **Step 1** 校验 `capabilities.can`。
- **Step 2** 订阅 `status/can`；用户打开监视后再订 `status/can/frames`（避免无谓刷屏）。
- **Step 3** UI：tunnel 开关、帧表、dropped；`allow_tx` 开关强警示（UI 侧保持默认关，实际以
  `status/can.allow_tx` 为准——联调剖面固件默认 `true`）。
- **Step 4** 发 `cmd/can`；仅明确确认后才用 `cmd/can/tx`。
- **Step 5** unmount 退订（尤其 `status/can/frames`）。

## 固件实现

- `mqtt/modules/can_mqtt.*`；挂钩 `ESP32Can` 嗅探/打包；见 [can/README](../../../../can/README.md)。
- 心跳帧：`DeepDogCanMqtt::EmitHeartbeat()`（周期 `DEEP_DOG_CAN_HEARTBEAT_MS`，见 `can/can_config.h`）。

## 验收

- [ ] 详情页可开 tunnel 看帧（桩阶段可 mock）
- [ ] 文档含 allow_tx 危险提示
- [ ] 无 capability 隐藏入口卡
- [ ] 空总线 / 总线无报文时，仍能在 `status/can/frames` 看到 `0x1F000000` 心跳帧
