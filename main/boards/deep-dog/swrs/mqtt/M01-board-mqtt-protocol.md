# M01 · deep-dog 板级 MQTT 协议

| 项 | 内容 |
|----|------|
| 路线图 ID | **M01** |
| 依赖 | [infra EMQX](../vision/infra.md) |
| 协议真源 | [protocol/deep-dog-mqtt.yml](./protocol/deep-dog-mqtt.yml) |
| 状态 | **文档 ✅**；固件客户端待 V-C03 起逐步实现 |
| 范围 | 契约与字段；**不含**固件实现 |

## 1. 目标

为网页 / App / 设备提供统一 MQTT 契约：Topic 按模块拆分，`device/info.capabilities` 声明启用项，支持将 deep-dog 裁剪为非机器狗产品（关 `dog`，保留其它模块）。

## 2. 产品原则

| 原则 | 说明 |
|------|------|
| 协议按模块拆分 | 各域独立 `cmd`/`status`，互不绑死 |
| 能力可裁剪 | 未编译模块 → `capabilities.<id>=false`，不订不发对应 Topic |
| Web 按能力显隐 | 订阅 `device/info` 后渲染面板 |
| YAML 为真源 | Markdown 只摘要；改字段先改 YAML |
| 密钥不下 MQTT | Immich Key / Broker 密码走 NVS；人脸大图不上 MQTT |

## 3. Broker 与前缀

见 [infra.md](../vision/infra.md)。

- 设备：`mqtt://192.168.31.25:1883`
- 前缀：`deepdiary/deep-dog/{device_id}/`
- 通用：cmd **QoS=1**、status **QoS=0**；变更即时发；部分 status **retain**（见 YAML）

## 4. 模块目录

| module_id | Topic 前缀 | 参考 | 契约 | 实现 |
|-----------|------------|------|------|------|
| `device` | `device/*` | diary DeviceInfo | ✅ | 随 C03 客户端 |
| `dog` | `dog/*` | DogControl / HTTP | ✅ 可裁剪 | HTTP 已有；MQTT 后接 |
| `stream` | `stream/*` | VisionFrameHub | ✅ | [C03](../vision/client/C03-mqtt-stream-control.md) |
| `face` | `face/*` | face_ai / `/api/face` | ✅ | 随 C03 |
| `imu` | `imu/*` | [thumble sensor](../../../deep-thumble/sensor/) BMI270 | ✅ | D9 + MQTT |
| `led` | `led/*` | [thumble led](../../../deep-thumble/led/) | ✅ | 更后 |
| `servo` | `servo/*` | [diary servo](../../../deep-diary/servo/) | ✅ | 更后 |
| `gimbal` | `gimbal/*` | [diary gimbal](../../../deep-diary/gimbal/) | ✅ | [C04](../vision/client/C04-mqtt-gimbal.md) |
| `handle` | `handle/*` | [diary handle](../../../deep-diary/handle/) 草稿 | ✅ 骨架 | 更后 |
| `touch` | `touch/status` | [touch_btn](../../touch_btn/) | ✅ | 驱动已有；MQTT 随客户端 |
| `can` | `can/*` | [can/](../../can/) TWAI；引脚源自 sparkbot UART 38/48 | ✅ 透传 | 更后（网页见 deep-trace `80-can-web-tunnel`） |
| `person` | `person/active` | P01 | 预留 | P01 |
| `track` | `track/cmd` | 人脸跟踪 | 预留 | 更后 |

### 裁剪规则

1. `capabilities.<id>=false` → 不订阅该域 `*/cmd`，不发布该域 `*/status`（及该域其它上行）。
2. Web 根据 `device/info` 隐藏未启用模块。
3. 硬依赖仅：`gimbal` → `servo` 驱动；`track` 可选依赖 `face` + `gimbal`/`dog`。
4. `touch` 与 `dog` **协议解耦**：只上报按键物理态；非狗项目可保留 touch。
5. `can` 可独立于 `dog` 开启（非狗项目仍可做 CAN 嗅探/透传）；**默认禁止网页下行注入**（`allow_tx=false`），电机总线开启注入极危险。

**非狗示例**：`dog=false`（可选 `track=false`），其余按硬件打开；需要总线监视时开 `can`。

## 5. Topic 树

```text
deepdiary/deep-dog/{device_id}/
├── device/info            ↑
├── device/status          ↑
├── dog/cmd|status         ↕
├── stream/cmd|status      ↕
├── face/cmd|status        ↕
├── imu/status             ↑
├── led/cmd|status         ↕
├── servo/cmd|status       ↕
├── gimbal/cmd|status      ↕
├── handle/cmd|status      ↕
├── touch/status           ↑
├── can/cmd|status         ↕ 透传开关 / 总线态
├── can/frames             ↑ 打包帧（网页显示）
├── can/tx                 ↓ 可选注入（默认关）
├── person/active          ↑ 预留
└── track/cmd              ↓ 预留
```

## 6. HTTP / 驱动映射

| MQTT | 现有 HTTP / 驱动 |
|------|------------------|
| `stream/cmd` start\|stop | `POST /api/vision_publish?mode=rtsp_push\|off` |
| `stream/status` | `GET /api/status` → `push_status` / `push_url` / `mode` |
| `face/cmd` | `POST /api/face_enable` |
| `face/status` | `GET /api/face`（`has_person` ≡ `has_face`） |
| `dog/cmd` | `POST /api/cmd?cmd=` |
| `dog/status` | `dog_initialized` + `/api/dog_status` 摘要 |
| `imu/status` | D9 BMI270 / thumble `ImuRawData`；未来可补 `/api/imu` |
| `led/*` | thumble/diary LedStripControl |
| `servo/*` | diary Servo |
| `gimbal/*` | diary Gimbal_setAngles / move* / getAngles |
| `touch/status` | TouchButtonController press/release/long_press |
| `handle/*` | 待手柄硬件；上报实际控制请求值 |
| `can/cmd` · `can/frames` · `can/tx` | [can/](../../can/) `ESP32Can` 嗅探/打包；网页对齐 deep-trace IoT「CAN Web Tunnel」 |

## 7. 样例 JSON（摘要）

完整 schema 见 YAML。下列供联调对照。

### device/info

```json
{
  "device_id": "deep-dog-dev",
  "firmware": "0.0.0",
  "ip": "192.168.31.211",
  "http_port": 8080,
  "capabilities": {
    "dog": true, "stream": true, "face": true, "imu": false,
    "led": false, "servo": false, "gimbal": false,
    "handle": false, "touch": true, "can": true
  },
  "ts": 1710000000
}
```

### stream / face

见 [C03](../vision/client/C03-mqtt-stream-control.md)。

### dog/cmd · dog/status

```json
{ "cmd": "forward", "ts": 1710000000 }
```

```json
{ "dog_initialized": true, "has_fault": false, "ts": 1710000000 }
```

`cmd` 枚举对齐 HTTP：`init` / `forward` / `back` / `stand` / `liedown` / `dance` / `stop_walk` / `disable`。

### imu/status

```json
{
  "ok": true,
  "accel_x": 0.1, "accel_y": -0.05, "accel_z": 9.8, "accel_g": 9.82,
  "gyro_x": 0.0, "gyro_y": 0.0, "gyro_z": 0.0,
  "pitch": 2.5, "roll": -1.2,
  "ts": 1710000000
}
```

单位：accel `m/s²`，gyro `dps`，角 `deg`。建议发布约 5–10 Hz。芯片 **BMI270**（对齐 thumble）。

### led/cmd · led/status

```json
{
  "mode": 1, "brightness": 128, "r": 0, "g": 255, "b": 0,
  "low_brightness": 16, "low_r": 0, "low_g": 0, "low_b": 0,
  "interval_ms": 500, "scroll_length": 3, "ts": 1710000000
}
```

`mode`：0 关 / 1 静态 / 2 闪烁 / 3 呼吸 / 4 滚动 / 5 系统状态。

### servo / gimbal

见 [C04](../vision/client/C04-mqtt-gimbal.md) 与 YAML。云台产品路径用 `gimbal/*`。

### handle/status

```json
{
  "connected": true,
  "source": "bt",
  "axes": { "lx": 0.0, "ly": 0.0, "rx": 0.0, "ry": 0.0 },
  "buttons": {
    "a": false, "b": false, "x": false, "y": false,
    "l1": false, "r1": false, "l2": 0.0, "r2": 0.0,
    "start": false, "select": false
  },
  "raw": {},
  "ts": 1710000000
}
```

上报**实际控制请求值**；轴表硬件选定后可 0.1.x 补丁。`handle/cmd`：`enable|disable|pair`。

### touch/status

```json
{
  "ok": true,
  "buttons": [
    { "id": 1, "pressed": false, "long_press": false, "last_event": "release" },
    { "id": 2, "pressed": true, "long_press": true, "last_event": "long_press" },
    { "id": 3, "pressed": false, "long_press": false, "last_event": "release" }
  ],
  "ts": 1710000000
}
```

状态变更发整包三键快照；`retain=true`。v0.1 无 `touch/cmd`。可选 `debug[]`：`value`/`baseline`/`abs_diff`。

### can（MQTT 透传 · 网页显示 CAN 帧）

**硬件**：esp-sparkbot 上 **GPIO38 / GPIO48** 原为 UART（`UART_ECHO_TXD/RXD`）；deep-dog 将其配置为 **CAN**（`CAN_TX_GPIO=38`、`CAN_RX_GPIO=48`，见 [`config.h`](../../config.h) / [`can/README.md`](../../can/README.md)）。经收发器接 CANH/CANL。

**网页**：订阅 `can/frames` 做实时帧表；产品需求对齐 deep-trace  
`docs/requirements/features/iot/80-can-web-tunnel.md`（CAN Web Tunnel）。本仓只定设备侧 MQTT 契约。

**注意**：电机总线可达 1 Mbps，**禁止默认全量透传**。须 `tunnel=true` + `max_hz` / `id_filter` / 批量打包；溢出计 `dropped`。

#### `can/cmd`（↓）

```json
{
  "tunnel": true,
  "mirror_tx": true,
  "allow_tx": false,
  "max_hz": 50,
  "batch_max": 32,
  "id_filter": [],
  "ext_only": true,
  "ts": 1710000000
}
```

| 字段 | 说明 |
|------|------|
| `tunnel` | 是否向 MQTT 上报帧 |
| `mirror_tx` | 是否把本机发出的 TX 也镜像进 `can/frames` |
| `allow_tx` | 是否允许网页经 `can/tx` 注入（**默认 false**） |
| `max_hz` | 上报批次上限（包/秒），防打爆 Broker |
| `batch_max` | 单包最多帧数 |
| `id_filter` | 空=不过滤；否则仅匹配这些 ID（十进制或 `0x…` 字符串） |
| `ext_only` | `true` 仅扩展帧（深狗电机为 29 位扩展帧） |

#### `can/status`（↑ retain）

```json
{
  "ok": true,
  "tunnel": true,
  "allow_tx": false,
  "bitrate_kbps": 1000,
  "tx_gpio": 38,
  "rx_gpio": 48,
  "bus_state": 0,
  "rx_err": 0,
  "tx_err": 0,
  "bus_err": 0,
  "dropped": 0,
  "ts": 1710000000
}
```

#### `can/frames`（↑ 核心透传）

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

对齐 `twai_message_t` / `CanFrame`：`identifier`、`extd`、`data_length_code`、`data[8]`。

#### `can/tx`（↓ 可选注入）

```json
{
  "frames": [
    {
      "id_hex": "0x12345678",
      "ext": true,
      "rtr": false,
      "dlc": 8,
      "data_hex": "0102030405060708"
    }
  ],
  "ts": 1710000000
}
```

仅当 `allow_tx=true` 时执行；否则忽略并可不改 `can/status`。

## 8. 与 deep-diary / thumble / sparkbot 的关系

| 来源 | 借鉴 | 不采用 |
|------|------|--------|
| [deep-diary/mqtt](../../../deep-diary/mqtt/) | Client/Config/Handler/Collector 分层；cmd QoS=1；重连；稀疏字段 | `Thumbler/` 扁平双 Topic、`tar_pitch/roll`、单 status 大包 |
| thumble LED/IMU | 模式 0–5、BMI270 `ImuRawData` 单位 | README 当唯一协议（本仓用 YAML） |
| diary Servo/Gimbal | API 语义 | 混进 LED 的扁平 cmd |
| touch_btn | 三键事件枚举 | 把狗动作结果塞进 touch 包 |
| [esp-sparkbot](../../../esp-sparkbot/config.h) | GPIO38/48 可作总线脚（原 UART） | 继续当 UART 底盘（deep-dog 已改为 CAN 电机） |
| deep-trace `80-can-web-tunnel` | 网页帧表 / 透传产品意图 | 不把 Web 实现放进本仓库 |

固件阶段新建 `main/boards/deep-dog/mqtt/`，**模式借鉴、协议独立**。

## 9. 安全

- 禁止仓库写入 Immich / Broker 明文密码。
- 人脸大图不上 MQTT（与 infra 一致）。
- CAN：`allow_tx` 默认关；全量无过滤透传禁止作为默认配置。

## 10. 验收（文档）

- [x] YAML 含 modules：device/dog/stream/face/imu/led/servo/gimbal/handle/touch/**can** + person/track 预留
- [x] `can/frames` 样例可供网页画帧表；含节流 / filter / `allow_tx` 约定
- [x] 各模块样例 JSON 与裁剪规则可读
- [x] C03 / C04 指向本文件与 YAML
- [ ] 固件实现（非本切片）

## 11. 非本切片

不写固件 MQTT 客户端；不移植 LED/Servo/Gimbal/Handle/IMU；不定死额外 GPIO；不实现跟脸闭环；不做网页 CAN Tunnel UI（属 deep-trace）。
