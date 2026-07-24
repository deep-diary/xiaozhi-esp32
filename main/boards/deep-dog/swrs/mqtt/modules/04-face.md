# 04 · face（人脸 · 检测中心页）

| 项 | 内容 |
|----|------|
| module_id | `face` |
| capabilities | `face`（入口卡）；跟踪区另看 `capabilities.track` |
| 路由建议 | `/device/:deviceId/modules/face` |
| 契约 | ready（检测）；跟踪见 [05-track](./05-track.md) ready（MQTT / actuator=none） |
| YAML | `face/cmd`、`face/status` |
| 说明 | 可与 stream 同 MQTT 客户端；**与 track 同详情页**（Topic 仍拆分） |

## 入口卡文案（设备页唯一人脸入口）

- 标题：人脸  
- 说明：检测 / 中心坐标；若 `capabilities.track` →「检测与跟踪」  
- **不出**独立 Track 卡（跟踪在本页 `#track`）

## 详情页目标（同页三区）

| 分区 | 条件 | 内容 |
|------|------|------|
| **检测** | `face` | `has_person`、`primary`、`faces[]`；`face/cmd.enabled` |
| **跟踪** | `track` | 见 [05-track](./05-track.md)；`#track` |
| **识别（只读）** | 有字段或 `person` | `faces[].display_name` / 可选订 `person/active`；无则灰显 |

识别与检测**可共存**：检测高频（约 2 Hz）；识别低频（建议 ≤0.5 Hz 或新人脸时）。识别失败不影响检测/跟踪。独立 Kiosk 人物页仍见 [13-person](./13-person.md)。

兼容：`/modules/track` → redirect → `/modules/face#track`。

## Topic（前端联调）

| Topic | 方向 | QoS | retain | 说明 |
|-------|------|-----|--------|------|
| `…/face/status` | ↑ | 0 | **false** | 有变化才发（约 2 Hz 检测） |
| `…/face/cmd` | ↓ | 1 | false | `{ "enabled": bool, "ts": int }` |
| `…/track/*` | — | — | — | 有 `track` 时同页订阅，见 05 |
| `…/person/active` | ↑ | 0 | true | 可选只读识别 |

默认 `deviceId=dev`：`deepdiary/deep-dog/dev/face/status|cmd`。

另读 `device/info` → `capabilities.face === true`。

v0.1 **不**在 `face/cmd` 加识别开关/Hz；识别策略在固件侧。

## 样例 JSON

**status**（坐标为**像素**，相对 `w`×`h`；与 HTTP `/api/face` 的 0–1 归一化不同）

```json
{
  "enabled": true,
  "has_person": true,
  "n": 1,
  "w": 240,
  "h": 240,
  "primary": { "cx": 120, "cy": 100, "score": 0.9 },
  "faces": [
    {
      "x0": 80, "y0": 60, "x1": 160, "y1": 140,
      "cx": 120, "cy": 100, "score": 0.9,
      "local_id": 2, "display_name": "#2"
    }
  ],
  "ts": 1710000000
}
```

**cmd**

```json
{ "enabled": true, "ts": 1710000000 }
```

`has_person` ≡ HTTP `has_face`。字段以 [YAML](../protocol/deep-dog-mqtt.yml) 为准。

## 与推流互斥（联调注意）

当前固件 `DEEP_DOG_FACE_AI_DURING_RTSP=0`：`stream` 为 `rtsp_push` 时 **不送人脸帧**，`has_person` 常为 false。

验真人脸前请先停推流：

```bash
/usr/bin/python3 scripts/deep_dog_mqtt_verify.py --via web --stop-stream --wait 5
```

`face/cmd` 开关始终可用（只改 `enabled`，不依赖是否正在检测）。

## Steps（前端）

- **Step 1** 校验 `capabilities.face`；决定是否渲染跟踪区（`track`）。
- **Step 2** 订阅 `face/status`（非 retain）；若有 track → 再订 `track/status`（见 05）。
- **Step 3** 检测区：有人/框表/主脸坐标（像素；可用 `cx/w`、`cy/h` 画 canvas）。
- **Step 4** 检测开关 → `face/cmd`；跟踪开关 → `track/cmd`。
- **Step 5** 识别只读：展示 `display_name`；可选订 `person/active`。
- **Step 6** unmount 退订本页全部业务 Topic。

## 固件实现

- 映射 `GET /api/face`、`POST /api/face_enable`（`DeepDogFaceAi*`）。
- [`mqtt/modules/face_mqtt`](../../../mqtt/modules/face_mqtt.h)：像素坐标 + on_change。
- 与 [02-stream](./02-stream.md) 同客户端。
- 识别（若启用）：低于检测频率的旁路；可派生 `person/active`。跟踪见 05。

## 验证脚本

```bash
/usr/bin/python3 scripts/deep_dog_mqtt_face_verify.py --via web --wait 12
/usr/bin/python3 scripts/deep_dog_mqtt_face_verify.py --via lan --wait 12
/usr/bin/python3 scripts/deep_dog_mqtt_face_verify.py --via both --wait 10
```

## 验收

### 固件

- [x] 发布 `face/status` / 接收 `face/cmd`
- [x] enable 开关与 `DeepDogFaceAiIsEnabled` 一致
- [ ] 停推流后人脸检测时 `has_person` 与 `primary.cx/cy` 合理（视场景）

### 前端

- [ ] 详情页可读状态、可开关检测
- [ ] 无 `face` capability 隐藏入口卡；无独立 Track 卡
- [ ] 有 `track` 时同页露出跟踪区（固件 MQTT ready / actuator=none）
