# 04 · face（人脸）

| 项 | 内容 |
|----|------|
| module_id | `face` |
| capabilities | `face` |
| 路由建议 | `/device/:deviceId/modules/face` |
| 契约 | ready |
| YAML | `face/cmd`、`face/status` |
| 说明 | 可与 stream 同 MQTT 客户端；详情页独立 |

## 入口卡文案

- 标题：人脸  
- 说明：是否有人、中心坐标、检测开关  

## 详情页目标

显示 `has_person`、`primary.cx/cy`、`faces[]`；开关检测 `enabled`。

## Topic（前端联调）

| Topic | 方向 | QoS | retain | 说明 |
|-------|------|-----|--------|------|
| `deepdiary/deep-dog/{deviceId}/face/status` | ↑ | 0 | **false** | 有变化才发（约 2 Hz 轮询检测） |
| `deepdiary/deep-dog/{deviceId}/face/cmd` | ↓ | 1 | false | `{ "enabled": bool, "ts": int }` |

默认 `deviceId=dev`：

- `deepdiary/deep-dog/dev/face/status`
- `deepdiary/deep-dog/dev/face/cmd`

另读 `device/info` → `capabilities.face === true`。

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

- **Step 1** 校验 `capabilities.face`。
- **Step 2** 订阅 `face/status`（非 retain，等下一帧）。
- **Step 3** 渲染有人/框表/主脸坐标（像素；可用 `cx/w`、`cy/h` 归一化画 canvas）。
- **Step 4** 开关发布 `face/cmd`。
- **Step 5** unmount 退订。

## 固件实现

- 映射 `GET /api/face`、`POST /api/face_enable`（`DeepDogFaceAi*`）。
- [`mqtt/modules/face_mqtt`](../../../mqtt/modules/face_mqtt.h)：像素坐标 + on_change。
- 与 [02-stream](./02-stream.md) 同客户端。

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

- [ ] 详情页可读状态、可开关
- [ ] 无 capability 隐藏入口卡
