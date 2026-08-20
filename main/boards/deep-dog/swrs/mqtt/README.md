# deep-dog MQTT

**可裁剪全功能模块板** MQTT 契约与前端规格。字段真源：[protocol/deep-dog-mqtt.yml](./protocol/deep-dog-mqtt.yml)。

## 前端怎么读

1. [frontend/00-device-page.md](./frontend/00-device-page.md) — 设备页：入口卡（无 detail）
2. [frontend/01-add-device-pairing.md](./frontend/01-add-device-pairing.md) — 之家：按配对码添加 / 解绑设备
3. [modules/](./modules/) — 各模块**独立详情页**（订阅/控制 Steps）
4. [M01](./M01-board-mqtt-protocol.md) — 总览、裁剪、Broker
5. [M02](./M02-mcp-call-control-plane.md) — MQTT `mcp_call` 统一控制面（motor 已落地）

| 顺序 | 模块文档 |
|------|----------|
| 00 | [pairing](./modules/00-pairing.md)（添加设备 / MAC `device_id`；无 Hub 卡） |
| 01 | [device](./modules/01-device.md) |
| 02 | [stream](./modules/02-stream.md)（V-C03） |
| 03 | [imu](./modules/03-imu.md) |
| 04 | [face](./modules/04-face.md)（检测中心页；含跟踪区） |
| 05 | [track](./modules/05-track.md)（**同 Face 页**，无独立入口卡） |
| 06 | [touch](./modules/06-touch.md) |
| 07 | [dog](./modules/07-dog.md) |
| 08 | [led](./modules/08-led.md) |
| 09 | [gimbal](./modules/09-gimbal.md)（V-C04） |
| 10 | [servo](./modules/10-servo.md) |
| 11 | [handle](./modules/11-handle.md)（双源；架构见 [../input/](../input/)） |
| 12 | [can](./modules/12-can.md) |
| 13 | [person](./modules/13-person.md) |
| 14 | [motor](./modules/14-motor.md)（单电机；MOT-03） |

IA：**入口卡 → 详情页**。卡上不做完整控制；模块 Topic 仅在详情页订阅。  
**Face + Track 同页**：设备页只出「人脸」卡；`/modules/track` redirect 到 `/modules/face#track`。

## 文档分层

| 层 | 读者 |
|----|------|
| `frontend/` + `modules/` | 前端 |
| modules 内「固件实现」 | 固件 |
| YAML | 双方字段真源 |

Broker 地址：[vision/infra.md](../vision/infra.md)。推流能力依赖 [C02](../vision/client/C02-device-push-stream.md)。

## 固件（V-C03）

源码：`main/boards/deep-dog/mqtt/`（`DEEP_DOG_MQTT_ENABLE`，默认 1）。

NVS 命名空间 `deep_dog_mqtt`：`broker_host` / `broker_port` / `device_id` / `client_id` / `username` / `password` / `bound` / `pair_code`。  
联调默认 broker `192.168.3.73:1883`（本机 Docker EMQX，`DeepServer/emqx/start.sh`；公共站可选 `broker.emqx.io:1883`，NVS 可覆盖）。**配网 AP 高级页**可改 `broker_host`/`broker_port`（见 [N03](../net/N03-wifi-portal-mqtt-broker.md)），免重编译。**未绑定**时 `device_id` 默认为 **`dev`**（Topic `deepdiary/deep-dog/dev/`、RTSP path `deep-dog/dev`）；**已绑定**后切换为 STA MAC 紧凑串（如 `aabbccddeeff`）。NVS 显式写入 `device_id` 可覆盖（仅联调）。绑定/解绑见 [00-pairing](./modules/00-pairing.md)。  
Hub / ingest 内网走 `ws://192.168.3.73:8083/mqtt`，与设备本机 EMQX 为同一 broker（TCP 1883 与 WS 8083 互通）。MQTTX 可直连 `192.168.3.73:1883` 或 `ws://192.168.3.73:8083/mqtt`。

### MQTTX / 脚本验收清单

**网页同款路径（推荐，内网）**：`ws://192.168.3.73:8083/mqtt`  
凭证用环境变量，勿写进仓库：`DEEP_DOG_MQTT_USER` / `DEEP_DOG_MQTT_PASS`。

```bash
# 本机 EMQX TCP（设备同路径；默认）
/usr/bin/python3 scripts/deep_dog/deep_dog_mqtt_verify.py --via lan --wait 20

# 外网 WSS（切回公共站时）
/usr/bin/python3 scripts/deep_dog/deep_dog_mqtt_verify.py --via web --wait 30 --start-stream
```

1. 连接本机 TCP `192.168.3.73:1883`（当前设备默认；或公共站 `broker.emqx.io:1883` / 网页 WS 对齐前端）。
2. 订阅 `deepdiary/deep-dog/{device_id}/device/info`（联调可用 `dev`）→ retain 可见 capabilities / ip / firmware。
3. 未绑定设备另订 `…/pairing/status` → retain 含 6 位 `code`；向 `…/pairing/cmd` 发 `{"action":"bound","ts":…}` 可模拟后端确认。
4. 订阅 `…/device/status` → 约 5s 心跳。
5. 订阅 `…/stream/status`；向 `…/stream/cmd` 发 QoS1：
   - `{"action":"start","ts":1710000000}` → status 进入 starting/streaming，MediaMTX 可拉。
   - `{"action":"stop","ts":1710000000}` → idle/off。
6. 发非法 `{"action":"nope"}` → 设备不重启，status.error 非空。
7. 断网重连后 info/status 重新 retain，stream/cmd 仍可控制。

Face / Track（同页联调）：

```bash
/usr/bin/python3 scripts/deep_dog/deep_dog_mqtt_verify.py --via web --stop-stream --wait 5
/usr/bin/python3 scripts/deep_dog/deep_dog_mqtt_face_verify.py --via both --wait 10
/usr/bin/python3 scripts/deep_dog/deep_dog_mqtt_track_verify.py --via both --wait 12
```

`device/info.capabilities.track=true`（`DEEP_DOG_TRACK_MQTT_ENABLE`）；`actuator=none`。详见 [05-track](./modules/05-track.md)。
