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

## Topic

| Topic | 方向 | QoS | retain |
|-------|------|-----|--------|
| `face/status` | ↑ | 0 | false |
| `face/cmd` | ↓ | 1 | false |

## 样例 JSON

**status**

```json
{
  "enabled": true,
  "has_person": true,
  "n": 1,
  "w": 640,
  "h": 480,
  "primary": { "cx": 320, "cy": 240, "score": 0.9 },
  "faces": [
    {
      "x0": 100, "y0": 80, "x1": 220, "y1": 240,
      "cx": 160, "cy": 160, "score": 0.9,
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

`has_person` ≡ HTTP `has_face`。坐标像素相对 `w`×`h`。以 [YAML](../protocol/deep-dog-mqtt.yml) 为准。

## Steps（前端）

- **Step 1** 校验 `capabilities.face`。
- **Step 2** 订阅 `face/status`。
- **Step 3** 渲染有人/框表/主脸坐标。
- **Step 4** 开关发布 `face/cmd`。
- **Step 5** unmount 退订。

## 固件实现

- 映射 `GET /api/face`、`POST /api/face_enable`。
- 建议与 [02-stream](./02-stream.md) 同客户端发布。

### 固件验收

- [ ] 有人时 `has_person` 与 `primary.cx/cy` 合理
- [ ] enable 开关与设备一致

## 验收（前端）

- [ ] 详情页可读状态、可开关
- [ ] 无 capability 隐藏入口卡
