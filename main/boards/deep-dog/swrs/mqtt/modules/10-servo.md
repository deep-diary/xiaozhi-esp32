# 10 · servo（裸舵机）

| 项 | 内容 |
|----|------|
| module_id | `servo` |
| capabilities | `servo` |
| 路由建议 | `/device/:deviceId/modules/servo` |
| 契约 | ready（字段）；实现 planned |
| YAML | `servo/cmd`、`servo/status` |
| 说明 | 调试用；产品云台走 [09-gimbal](./09-gimbal.md) |

## 入口卡文案

- 标题：舵机  
- 说明：多路裸舵机调试  

## 详情页目标

按 index 写角度 / attach；列表回读。

## Topic

| Topic | 方向 | QoS | retain |
|-------|------|-----|--------|
| `servo/status` | ↑ | 0 | true |
| `servo/cmd` | ↓ | 1 | false |

## 样例 JSON

```json
{ "index": 0, "angle": 90, "attach": true, "type": 180, "ts": 1710000000 }
```

```json
{
  "servos": [
    { "index": 0, "angle": 90, "attached": true, "type": 180, "min": 0, "max": 180 }
  ],
  "ts": 1710000000
}
```

以 [YAML](../protocol/deep-dog-mqtt.yml) 为准。

## Steps（前端）

- **Step 1** 校验 `capabilities.servo`。
- **Step 2** 订阅 `servo/status`。
- **Step 3** 列表 + 单路角度控制；发 `servo/cmd`。
- **Step 4** unmount 退订。

## 固件实现

- planned；diary Servo API。云台场景勿引导用户只用本页。

## 验收

- [ ] 前端可按文档做调试页桩
- [ ] 无 capability 隐藏入口卡
