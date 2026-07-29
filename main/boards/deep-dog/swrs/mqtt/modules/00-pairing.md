# 00 · pairing（设备配对）

| 项 | 内容 |
|----|------|
| module_id | `pairing` |
| capabilities | （核心；未绑定设备始终进入配对流） |
| 路由建议 | 无独立 Hub 卡；由网页「添加设备」消费 |
| 契约 | **已实现待实测**；字段以 [YAML](../protocol/deep-dog-mqtt.yml) 为准 |
| 对齐 | deep-trace REQ-IOT-142：`/Volumes/MacExtStorage/projects/deep-trace/docs/requirements/features/iot/hub/142-home-add-device-by-pair-code.md`；REQ-IOT-143：`/Volumes/MacExtStorage/projects/deep-trace/docs/requirements/features/iot/143-device-pair-bind-api.md` |

## 目标

多台设备刷同一固件时，MQTT Topic 前缀必须互不冲突；用户在网页添加设备时**只输入 6 位配对码**，不手输 MAC / `device_id`。

## 身份约定

| 项 | 约定 |
|----|------|
| 稳定 `device_id` | STA MAC **紧凑串**：小写、无冒号，如 `aabbccddeeff` |
| Topic 前缀 | `deepdiary/deep-dog/{device_id}/` |
| `device/info.mac` | 可读形式 `AA:BB:…`（展示用） |
| NVS 覆盖 | 命名空间 `deep_dog_mqtt` 的 `device_id` 仍可强制为 `dev` 等，**仅联调**；生产默认不写死 `dev` |
| `client_id` | 可继续带 MAC 后缀，避免 Broker 会话冲突 |

## 通道（混合）

1. 设备生成 6 位数字码，语音/屏显播报，经 MQTT 上报 `pairing/status`。
2. 网页只调 Django HTTP 绑定 API（见 REQ-IOT-142/143）。
3. 后端绑定成功后下行 `pairing/cmd`；设备写 NVS `bound=true`，退出配对播报循环。

**不**走官方小智 OTA `/activate`；业务 MQTT 与配对共用同一 Broker。

## 状态机

```text
上线 → 读 NVS bound
  ├─ bound=true  → 正常业务；pairing/status 可发 bound=true（无 code 或清空）
  └─ bound=false → 生成/复用 6 位码 → 播报 → publish pairing/status(retain)
                   → 订 pairing/cmd → 收到 action=bound → NVS bound=true → 停播报
                   → 收到 action=unbind → NVS bound=false → 重新进入配对
```

码有效期由后端 PendingPairing TTL 约束（建议 10～15 min）；设备可周期性重播同一码，或超时后换新码并重新 retain 上报。

## Topic

| Topic | 方向 | QoS | retain |
|-------|------|-----|--------|
| `pairing/status` | ↑ | 0 | true |
| `pairing/cmd` | ↓ | 1 | false |

前缀：`deepdiary/deep-dog/{device_id}/`。

## 字段表 · `pairing/status`

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `device_id` | string | 是 | MAC 紧凑串 |
| `mac` | string | 否 | `AA:BB:…` |
| `code` | string | 未绑定时是 | 恰好 6 位数字字符；`bound=true` 时可省略或空 |
| `bound` | bool | 是 | 是否已绑定到某用户/之家（设备侧 NVS） |
| `ts` | int | 是 | Unix 秒 |
| `ts_iso` | string | 否 | UTC ISO8601 |

## 字段表 · `pairing/cmd`

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `action` | string | 是 | `bound` \| `unbind` |
| `ts` | int | 是 | Unix 秒 |

稀疏 cmd：只带需变更的键；`action` 必填。

## 样例

未绑定：

```json
{
  "device_id": "aabbccddeeff",
  "mac": "AA:BB:CC:DD:EE:FF",
  "code": "482913",
  "bound": false,
  "ts": 1710000000,
  "ts_iso": "2024-03-09T12:00:00Z"
}
```

已绑定后 status（可选清理 code）：

```json
{
  "device_id": "aabbccddeeff",
  "mac": "AA:BB:CC:DD:EE:FF",
  "bound": true,
  "ts": 1710000060
}
```

下行绑定确认：

```json
{ "action": "bound", "ts": 1710000055 }
```

解绑：

```json
{ "action": "unbind", "ts": 1710001000 }
```

## 固件实现（需求约定，待开发）

- 默认 `device_id`：从 STA MAC 生成紧凑串；仅当 NVS 显式配置时覆盖。
- NVS `deep_dog_mqtt`：增加 `bound`（bool）；可选缓存当前 `pair_code`。
- 未绑定：上线/重连后 retain 发 `pairing/status`；订阅 `pairing/cmd`。
- 播报：逐位播放数字音（可复用小智 `OGG_0`…`OGG_9` 资源，若板级可用）；无扬声器时至少日志/屏显。
- 绑定后仍用同一 `device_id` 发 `device/info` 等业务 Topic，**不**改前缀。

## 验收

- [ ] 两台设备默认 Topic 前缀不同（各自 MAC）
- [ ] 未绑定设备 `pairing/status` retain 含 6 位 `code`
- [ ] 收到 `action=bound` 后不再要求用户输码即可正常业务；重连 `bound=true`
- [ ] 联调可 NVS 强制 `device_id=dev`，与正式默认语义文档区分清楚

## 非目标

- 官方 xiaozhi.me / OTA 激活兼容
- 网页手输 MAC 作为主路径（调试见 REQ-IOT-140）
