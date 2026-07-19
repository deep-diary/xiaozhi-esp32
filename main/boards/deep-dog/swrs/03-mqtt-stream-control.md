# 03 · MQTT 视频流开关控制

| 项 | 内容 |
|----|------|
| 优先级 | P0 |
| 依赖 | [02 设备视频流上传](./02-camera-stream-upload.md)、[00 EMQX](./00-shared-infra.md) |
| 代码落点 | 板内订阅/处理；尽量复用工程已有 MQTT 客户端能力 |
| 验收 | 网页或 MQTT 客户端可远程开/关推流 |

## 1. 背景

设备常驻推流费带宽与算力，需要远程开关。工程内已有 `MqttProtocol`（面向小智云端语音通道）。本需求优先：**复用已有 MQTT 连接或板内另起局域网 EMQX 客户端**，避免再造一套网络栈。

## 2. 目标

- 通过 MQTT 命令启动 / 停止 [02] 中的推流。
- 设备上报当前推流状态，便于前端与调试。

## 3. 范围

**包含**

- Topic 约定、JSON 命令与状态
- 与开机默认推流配置的优先级关系
- 网页经 `wss://mqtt-ws.deep-diary.com/mqtt` 下发命令的联通路径说明

**不包含**

- 云台、人脸
- 修改小智官方 MQTT 业务协议语义（若复用同一连接，板内用独立 topic，互不干扰）

## 4. 架构决策（建议）

| 方案 | 说明 | 建议 |
|------|------|------|
| A. 复用 `MqttProtocol` 连接 | 少一条 TCP；但 broker 可能是小智云而非家里 EMQX | 仅当设备 MQTT 已指向家里 EMQX 时可行 |
| B. 板内独立 MQTT 客户端连家里 EMQX | 与语音云解耦；ESP 直连 `192.168.31.25:1883` | **推荐**（符合 [00] 设备首选局域网） |

实现前在板内用薄封装（如 `DiaryBrainMqtt`），topic 处理与推流模块解耦。

## 5. 协议草案

**命令** `deepdiary/diary-brain/<device_id>/stream/cmd`

```json
{ "action": "start" | "stop", "ts": 1710000000 }
```

**状态** `deepdiary/diary-brain/<device_id>/stream/status`

```json
{
  "state": "idle" | "starting" | "streaming" | "error",
  "url": "rtmp://192.168.31.25:1935/diary-brain/<device_id>",
  "error": null,
  "ts": 1710000000
}
```

规则：

- 收到 `start`：若已在推流则幂等成功并刷新 status。
- 收到 `stop`：停止推流并上报 `idle`。
- 开机默认推流开启时，联网后先上报 `streaming`；之后仍接受远程 `stop`。

## 6. 功能需求

| ID | 需求 | 说明 |
|----|------|------|
| CTL-01 | 远程开 | 订阅 cmd，执行 start，外部可拉流 |
| CTL-02 | 远程关 | 执行 stop，拉流端无新画面 |
| CTL-03 | 状态上报 | 状态变化与周期性（如 30s）心跳均可接受，至少保证边沿上报 |
| CTL-04 | 鉴权 | 使用 EMQX 用户名密码；ACL 限制仅本 `device_id` |
| CTL-05 | 复用优先 | 调研并文档化：最终采用方案 A 或 B，及与现有 `mqtt_protocol` 的关系 |

## 7. 验收标准

- [ ] 用 MQTTX / 网页 WS 向 `stream/cmd` 发 `start`/`stop`，推流随之变化
- [ ] `stream/status` 与真实状态一致
- [ ] 外网网页经 `mqtt-ws` 控制内网设备成功（路径见 [00](./00-shared-infra.md)）
- [ ] 错误命令（未知 action）被忽略或返回明确 error，不崩溃

## 8. 开放问题

1. 小智云 MQTT 与家里 EMQX 是否会同时存在？若会，板内独立客户端（方案 B）更稳妥。
2. 是否需要保留「本地按键长按关推流」作为离线兜底？（非必须）
