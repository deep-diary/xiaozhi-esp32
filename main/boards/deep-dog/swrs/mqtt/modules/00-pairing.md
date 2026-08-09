# 00 · pairing（设备配对）

| 项 | 内容 |
|----|------|
| module_id | `pairing` |
| capabilities | （核心；显式触发配对流） |
| 路由建议 | 无独立 Hub 卡；由网页「添加设备」消费 |
| 契约 | **已实现待实测**；字段以 [YAML](../protocol/deep-dog-mqtt.yml) 为准 |
| 对齐 | deep-trace REQ-IOT-142；REQ-IOT-143 |

## 目标

多台设备刷同一固件时，MQTT Topic 前缀必须互不冲突；用户在网页添加设备时**只输入 6 位配对码**，不手输 MAC / `device_id`。

**须先在设备上进入配对模式**（MCP 语音 / 长按1+轻触2），设备才会播报码并 retain 上报 `code`。

## 身份约定

| 项 | 约定 |
|----|------|
| 未绑定 `device_id` | **`dev`**（联调默认；Topic `deepdiary/deep-dog/dev/`） |
| 已绑定 `device_id` | STA MAC **紧凑串**：小写、无冒号，如 `aabbccddeeff` |
| Topic 前缀 | `deepdiary/deep-dog/{device_id}/` |
| RTSP / HLS path | `deep-dog/{device_id}`（与 MQTT 前缀 id 对齐） |
| `device/info.mac` | 可读形式 `AA:BB:…`（展示用） |
| NVS | 命名空间 `deep_dog_mqtt`：`bound`、`device_id`（绑定写入 MAC，解绑清除）、`pair_code`、`pro_pairing_mqtt` |
| `client_id` | 可继续带 MAC 后缀，避免 Broker 会话冲突 |
| 绑定切换 | 收到 `pairing/cmd` `bound`/`unbind` 后重载 MQTT 前缀与推流 URL |

## 通道（混合）

1. 用户显式触发配对 → 设备生成 6 位码，**屏显 + 语音**播报，经 MQTT 上报 `pairing/status`（retain，含 `code`）。
2. 网页只调 Django HTTP 绑定 API（REQ-IOT-142/143）。
3. 后端绑定成功后下行 `pairing/cmd` `action=bound`；设备写 NVS `bound=true`，停止配对播报。
4. 解绑：MCP / 长按1+轻触3 → 设备上行 `pairing/request` `action=unbind` → 后端解绑并下行 `pairing/cmd` `action=unbind`。

**不**走官方小智 OTA `/activate`；业务 MQTT 与配对共用同一 Broker。

## 状态机

```text
上线 → 读 NVS bound
  ├─ bound=true  → publish pairing/status bound=true（无 code）；正常业务
  └─ bound=false → 静默（不自动播码、不重播）
                   │
                   ├─ MCP start_pairing / hold1_tap2
                   │     → 生成/复用码 → Alert 屏显 + 语音 → retain pairing/status
                   │     → 会话内 45s 重播
                   │
                   ├─ pro_pairing_mqtt=true（NVS）
                   │     → 未绑定时也 retain 带 code（无语音，专业/调试）
                   │
                   ├─ 收到 pairing/cmd bound → NVS bound=true → 屏显「绑定成功」→ 停重播
                   │
                   └─ MCP unbind / hold1_tap3 → pairing/request unbind
                         → 后端 unbind → pairing/cmd unbind → NVS bound=false → 静默
```

码有效期由后端 PendingPairing TTL 约束（建议 10～15 min）；配对会话内可周期性重播同一码。

## Topic

| Topic | 方向 | QoS | retain |
|-------|------|-----|--------|
| `pairing/status` | ↑ | 0 | true |
| `pairing/request` | ↑ | 1 | false |
| `pairing/cmd` | ↓ | 1 | false |

前缀：`deepdiary/deep-dog/{device_id}/`。

## 字段表 · `pairing/status`

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `device_id` | string | 是 | MAC 紧凑串 |
| `mac` | string | 否 | `AA:BB:…` |
| `code` | string | 配对会话/pro 模式 | 恰好 6 位数字；`bound=true` 时省略 |
| `bound` | bool | 是 | 是否已绑定 |
| `ts` | int | 是 | Unix 秒 |

## 字段表 · `pairing/request`

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `action` | string | 是 | `unbind` |
| `device_id` | string | 是 | MAC 紧凑串 |
| `ts` | int | 是 | Unix 秒 |

## 字段表 · `pairing/cmd`

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `action` | string | 是 | `bound` \| `unbind` |
| `ts` | int | 是 | Unix 秒 |

## 固件实现

| 项 | 落地 |
|----|------|
| MQTT 模块 | [`pairing_mqtt.cc`](../../../mqtt/modules/pairing_mqtt.cc) |
| MCP | [`pairing/pairing_mcp.cc`](../../../pairing/pairing_mcp.cc)：`self.device.start_pairing` / `self.device.unbind` |
| 触摸 | `hold1_tap2` 配对；`hold1_tap3` 解绑（[`esp_sparkbot_board.cc`](../../../esp_sparkbot_board.cc)） |
| 播报 | 逐位 `OGG_0`…`OGG_9` + `Application::Alert` 屏显；会话内 45s 重播 |

## 联调前置

```bash
MQTT_HOST=192.168.31.25 MQTT_PORT=8083 MQTT_USE_TLS=false MQTT_PATH=/mqtt \
  python manage.py run_pairing_ingest
```

前端 UX：[`../frontend/01-add-device-pairing.md`](../frontend/01-add-device-pairing.md)

## 验收

- [ ] 开机未绑定：无自动播码
- [ ] MCP / hold1_tap2：屏显 + 语音 + ingest 收到 pending
- [ ] 网页输入码：绑定成功，屏显「绑定成功」
- [ ] 已绑定 MCP / hold1_tap2：提示已绑定
- [ ] MCP unbind / hold1_tap3：后端解绑 + 设备收到 unbind
- [ ] `pro_pairing_mqtt=1`：未绑定时 retain 带 code

## 非目标

- 官方 xiaozhi.me / OTA 激活兼容
- 网页手输 MAC 作为主路径
