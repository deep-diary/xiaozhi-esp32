# 05 · track（人脸跟踪）

| 项 | 内容 |
|----|------|
| module_id | `track` |
| capabilities | `track` |
| **前端 UI** | **与 Face 同页**（无独立入口卡） |
| 主路由 | `/device/:deviceId/modules/face`（页内 `#track`） |
| 兼容路由 | `/device/:deviceId/modules/track` → **redirect** → `/modules/face#track` |
| 契约 | **ready（MQTT / actuator=none）** |
| YAML | `track/cmd`、`track/status` |
| 依赖 | **必须** `face`（检测）；可选 `gimbal` / `dog` 作执行器（本阶段未接） |

## 信息架构

| 层 | 说明 |
|----|------|
| 设备页 | **不出** Track 入口卡；仅 [04-face](./04-face.md)「人脸」卡；`capabilities.track` 时卡说明可写「含跟踪」 |
| 详情页 | Face 中心页分区：**检测 → 跟踪 → 识别（只读）** |

Topic / capabilities 仍拆分：`face/*` ≠ `track/*`，便于无云台/狗时裁掉 `track`。

## 管线（检测 / 跟踪 / 识别）

1. **检测（face，高频）**：约 2 Hz；产出 `primary` / `faces[]`。跟踪依赖其开启且有目标。
2. **跟踪（track，v0.1）**：设备内直接读 face snapshot（不订 `face/status`）；`following = enabled && face_on && has_primary`；**`actuator=none`**（不驱动云台/狗）。
3. **识别（低频，可共存）**：≤0.5 Hz 或新人脸时；结果挂 `faces[].display_name` / [13-person](./13-person.md)；**失败不影响检测与跟踪**。

检测关闭或无人时：允许 `track/cmd enable`，但 `following=false`（`error` 可为 `face_off` / `no_target`）。

## 入口卡文案

**无独立卡。** 见 [04-face](./04-face.md)「入口卡文案」。

## 详情页目标（Face 页 · 跟踪区）

- 开关跟踪（`track/cmd`）
- 展示 `enabled` / `following` / 目标点（订 `track/status` + 只读 `face/status.primary`）
- `capabilities.track !== true` 时隐藏本区
- `actuator=none` 时 UI 可提示「仅状态，无云台/狗转向」

## Topic

全路径前缀：`deepdiary/deep-dog/{deviceId}/`

| Topic | 方向 | QoS | retain | 说明 |
|-------|------|-----|--------|------|
| `track/cmd` | ↓ | 1 | false | `{ "action": "enable"\|"disable", "ts": ... }` |
| `track/status` | ↑ | 0 | false | on_change，约 2 Hz 轮询 |
| `face/status` | ↑ | 0 | false | 同页只读，取 primary |

默认 `deviceId=dev`：`deepdiary/deep-dog/dev/track/status|cmd`。

## 样例 JSON

**cmd**

```json
{ "action": "enable", "ts": 1710000000 }
```

**status**（v0.1，执行器 none）

```json
{
  "enabled": true,
  "following": false,
  "actuator": "none",
  "error": "no_target",
  "ts": 1710000000
}
```

有 primary 时：

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

- `target.cx/cy`：像素，与 face 一致
- `actuator`：本阶段恒为 `"none"`；日后可为 `gimbal` | `dog`
- `error`：可选提示字符串（`face_off` / `no_target`）

## Steps（前端 · 在 Face 同页）

- **Step 1** 进入 `/modules/face`；若从 `/modules/track` 进入则 redirect + `#track`。
- **Step 2** 若 `capabilities.track`：订阅 `track/status`；与检测区共用已订的 `face/status`。
- **Step 3** 渲染跟踪开关、`following`、目标十字（可用 face `primary` 或 status.`target`）。
- **Step 4** 发 `track/cmd`；检测未开时 UI 可提示「请先开启人脸检测」。
- **Step 5** unmount 时退订本页所订 Topic（含 track）。

## 固件实现

- [`mqtt/modules/track_mqtt`](../../../mqtt/modules/track_mqtt.h)：`DEEP_DOG_TRACK_MQTT_ENABLE`（默认 1）→ `capabilities.track=true`。
- 轮询 `DEEP_DOG_MQTT_TRACK_POLL_INTERVAL_US`（默认 500ms）；指纹 on_change；cmd 后 force 发布。
- 读 `DeepDogFaceAiCopySnapshot` / `IsEnabled`；不自订 MQTT `face/status`。
- **非目标（本阶段）**：真实 pan/tilt 或狗转向；按 `local_id` 指定目标（仍跟 primary）。

## 验证脚本

联调顺序：`stream stop` → `face/cmd enabled=true` → `track/cmd enable`。

```bash
/usr/bin/python3 scripts/deep_dog_mqtt_verify.py --via web --stop-stream --wait 5
/usr/bin/python3 scripts/deep_dog_mqtt_face_verify.py --via web --wait 8
/usr/bin/python3 scripts/deep_dog_mqtt_track_verify.py --via both --wait 12
```

## 验收

- [x] 契约 ready（MQTT / actuator=none）；无独立 Track 入口卡
- [x] 同页 IA、`track/status`、与识别共存写清
- [x] `/modules/track` redirect 约定写清
- [x] 前端可据 `capabilities.track` 露出跟踪区（无真实转向）
