# M01 · deep-dog 板级 MQTT 协议（总览）

| 项 | 内容 |
|----|------|
| 路线图 ID | **M01** |
| 依赖 | [infra EMQX](../vision/infra.md) |
| 协议真源 | [protocol/deep-dog-mqtt.yml](./protocol/deep-dog-mqtt.yml) |
| 前端入口 | [frontend/00-device-page.md](./frontend/00-device-page.md) → [modules/](./modules/) |
| 范围 | 原则、裁剪、索引；**字段细节与 UI Steps 在 modules** |

## 1. 目标

统一网页 / App / 设备 MQTT 契约：Topic 按模块拆分；`device/info.capabilities` 可裁剪（非狗可关 `dog`）。

## 2. 前端信息架构（拍板）

| 层级 | 职责 |
|------|------|
| **设备页** | Device Basic + **模块入口卡片**（不展示 detail、不做模块控制） |
| **模块详情页** | 点卡进入；订阅/发布本模块 Topic |

详见 [00-device-page](./frontend/00-device-page.md)。入口卡默认**不订**业务 Topic。

## 3. 原则

| 原则 | 说明 |
|------|------|
| 协议按模块拆分 | 各域独立 `cmd`/`status` |
| 能力可裁剪 | `capabilities.<id>=false` → 不订不发、前端不出卡 |
| YAML 为真源 | 改字段先改 YAML |
| 密钥不下 MQTT | Immich / Broker 密码走 NVS；人脸大图不上 MQTT |

## 4. Broker 与前缀

见 [infra.md](../vision/infra.md)。

- 设备：`mqtt://192.168.31.25:1883`
- 网页：`wss://mqtt-ws.deep-diary.com/mqtt`
- 前缀：`deepdiary/deep-dog/{device_id}/`
- **默认 `device_id`**：STA MAC 紧凑串（小写无冒号）；NVS 可覆盖为 `dev` 仅联调。见 [00-pairing](./modules/00-pairing.md)。
- cmd QoS=1；status QoS=0；部分 status retain（见 YAML）

## 5. 模块索引（前端推荐顺序）

| 顺序 | module_id | 文档 | 契约 |
|------|-----------|------|------|
| — | pairing | [00-pairing](./modules/00-pairing.md) | defined；**无 Hub 卡**；网页添加设备用 |
| 页头 | device | [01-device](./modules/01-device.md) | ready |
| 1 | stream | [02-stream](./modules/02-stream.md)（V-C03；含 face/track overlay UI） | ready |
| 2 | imu | [03-imu](./modules/03-imu.md) | ready / D9 |
| 3 | face | [04-face](./modules/04-face.md)（含原 person：Immich/打招呼） | ready |
| — | track | [05-track](./modules/05-track.md) | ready；**无入口卡**；UI 在 stream |
| 4 | touch | [06-touch](./modules/06-touch.md) | ready |
| 5 | dog | [07-dog](./modules/07-dog.md) | ready |
| 6 | led | [08-led](./modules/08-led.md) | planned |
| 7 | gimbal | [09-gimbal](./modules/09-gimbal.md)（V-C04） | ready / 更后 |
| 8 | servo | [10-servo](./modules/10-servo.md) | ready |
| 9 | handle | [11-handle](./modules/11-handle.md) | 骨架 |
| 10 | can | [12-can](./modules/12-can.md) | planned |
| — | person | [13-person](./modules/13-person.md) | 并入 face；**无入口卡** |

### 裁剪规则

1. `capabilities.<id>=false` → 设备不出入口卡；固件不订不发该域。
2. `gimbal` 依赖 `servo` 驱动；`track` **依赖** `face`，可选 `gimbal`/`dog`；Track **不单独出入口卡**。
3. `touch` 与 `dog` 协议解耦。
4. `can` 可独立于 `dog`；`allow_tx` 默认关。

## 6. Topic 树（摘要）

```text
deepdiary/deep-dog/{device_id}/
├── pairing/status|cmd
├── device/info|status
├── stream|face|dog|led|servo|gimbal|handle  …/cmd|status
├── imu/status · touch/status
├── can/cmd|status|frames|tx
├── person/active (reserved) · track/cmd|status (MQTT ready / actuator=none)
```
完整字段 → YAML；HTTP/驱动映射与样例 → 各 modules 文档。

## 7. 与 diary / thumble

- **借**：MQTT 客户端分层、QoS、重连、稀疏 cmd。
- **不借**：Thumbler 扁平双 Topic、单 status 大包。
- 灯带/IMU/舵机/云台参考 thumble、diary 实现，协议独立。

## 8. 安全

- 禁止仓库写明文密钥。
- CAN 网页注入默认禁止；人脸大图不上 MQTT。

## 9. 验收（文档）

- [x] 入口卡 + 独立详情页 IA 写清
- [x] 每模块一份 modules 文档含前端 Steps
- [x] 无并行 M02/M03；V-C03/C04 落在 02-stream / 09-gimbal
