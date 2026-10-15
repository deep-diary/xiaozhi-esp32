# 05 · track（人脸跟踪）

| 项 | 内容 |
|----|------|
| module_id | `track` |
| capabilities | `track` |
| **前端 UI** | **与 Stream 同页**（无独立入口卡） |
| 主路由 | `/device/:deviceId/modules/stream`（页内 `#track`） |
| 兼容路由 | `/device/:deviceId/modules/track` → **redirect** → `/modules/stream#track` |
| 契约 | **ready（MQTT / actuator=none）** |
| YAML | `track/cmd`、`track/status` |
| 依赖 | **必须** `face`（检测）；可选 `gimbal` / `dog`（本阶段未接） |

## 信息架构

| 层 | 说明 |
|----|------|
| 设备页 | **不出** Track 入口卡 |
| 详情页 | [02-stream](./02-stream.md) 叠加区：跟踪开关 + 目标十字 |
| Face 页 | 不做跟踪主 UI（见 [04-face](./04-face.md)） |

Topic 仍拆分：`face/*` ≠ `track/*`。

## 管线

1. **检测（face）**：`pipeline=live` 时可较高频；产出 `primary` / `faces[]`。多人取 **score 最高**。
2. **跟踪（track）**：读 face snapshot；`following = enabled && face_on && has_primary`；`actuator=none`。
3. **识别**：低频；挂 `display_name`；失败不影响跟踪。

检测关闭或无人：可 `track/cmd enable`，但 `following=false`（`face_off` / `no_target`）。

## Topic

前缀：`deepdiary/deep-dog/{deviceId}/`

| Topic | 方向 | QoS | retain |
|-------|------|-----|--------|
| `track/cmd` | ↓ | 1 | false |
| `track/status` | ↑ | 0 | false |

## 样例

**cmd**

```json
{ "action": "enable", "ts": 1710000000 }
```

```json
{ "action": "disable", "ts": 1710000000 }
```

**status**

```json
{
  "enabled": true,
  "following": true,
  "target": { "cx": 120, "cy": 100, "local_id": 2 },
  "actuator": "none",
  "error": "",
  "ts": 1710000000
}
```

无目标时：`"following": false, "error": "no_target"`。

## Steps（前端 · Stream 同页）

- **Step 1** 进入 Stream；从 `/modules/track` 则 redirect + `#track`。
- **Step 2** `capabilities.track` 时订 `track/status`，与 `face/status` 共用。
- **Step 3** 跟踪开关、`following`、目标十字。
- **Step 4** 发 `track/cmd`；检测未开可提示先开人脸。
- **Step 5** unmount 退订。

## 固件

- `DEEP_DOG_TRACK_MQTT_ENABLE`；轮询 on_change；跟最高 score primary。
- 本阶段无真实 pan/tilt。

## 验收

- [x] MQTT ready / actuator=none；无独立入口卡
- [ ] UI 宿主为 Stream 页
- [ ] `/modules/track` → `/modules/stream#track`
