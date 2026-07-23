# 06 · touch（触摸键）

| 项 | 内容 |
|----|------|
| module_id | `touch` |
| capabilities | `touch` |
| 路由建议 | `/device/:deviceId/modules/touch` |
| 契约 | ready（字段）；驱动已有 |
| YAML | `touch/status` |
| 参考 | [touch_btn](../../../touch_btn/) |

## 入口卡文案

- 标题：触摸键  
- 说明：三键按下 / 长按状态  

## 详情页目标

只读可视化三键 `pressed` / `long_press` / `last_event`。v0.1 无 cmd。

## Topic

| Topic | 方向 | QoS | retain |
|-------|------|-----|--------|
| `touch/status` | ↑ | 0 | true |

## 样例 JSON

```json
{
  "ok": true,
  "buttons": [
    { "id": 1, "pressed": false, "long_press": false, "last_event": "release" },
    { "id": 2, "pressed": true, "long_press": true, "last_event": "long_press" },
    { "id": 3, "pressed": false, "long_press": false, "last_event": "release" }
  ],
  "ts": 1710000000
}
```

以 [YAML](../protocol/deep-dog-mqtt.yml) 为准。可选 `debug[]`。

## Steps（前端）

- **Step 1** 校验 `capabilities.touch`。
- **Step 2** 订阅 `touch/status`（retain，晚进页也有当前态）。
- **Step 3** 三键 UI 高亮。
- **Step 4** unmount 退订。

## 固件实现

- 挂钩 `TouchButtonController` 事件，变更时发整包三键快照。
- 与 `dog` 业务解耦：本 Topic 只反映物理态。

## 验收

- [ ] 按下时详情页即时更新
- [ ] 无 capability 隐藏入口卡
