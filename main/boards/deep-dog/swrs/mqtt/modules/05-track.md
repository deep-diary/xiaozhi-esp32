# 05 · track（人脸跟踪）

| 项 | 内容 |
|----|------|
| module_id | `track` |
| capabilities | `track`（若未单独暴露，可用 planned 隐藏） |
| 路由建议 | `/device/:deviceId/modules/track` |
| 契约 | **reserved / planned** |
| YAML | `track/cmd` |
| 依赖 | 可选 `face` + `gimbal` / `dog` |

## 入口卡文案

- 标题：跟踪  
- 说明：根据人脸中心跟随（规划中）  

## 详情页目标

规划：enable/disable 跟踪；展示是否跟随中。消费 [04-face](./04-face.md) 的 `primary`（可在本页只读订阅 `face/status`，或仅显示「请先打开人脸页」——**推荐本页可订 `face/status` 只读**以便闭环调试）。

## Topic

| Topic | 方向 | QoS | retain |
|-------|------|-----|--------|
| `track/cmd` | ↓ | 1 | false |
| `face/status` | ↑ | 0 | false | 可选只读，供显示目标点 |

## 样例 JSON

```json
{ "action": "enable", "ts": 1710000000 }
```

以 [YAML](../protocol/deep-dog-mqtt.yml) 为准。

## Steps（前端）

- **Step 1** 无 capability / planned：入口卡标记「即将推出」或隐藏（产品二选一；**默认隐藏**）。
- **Step 2** 若开放：订阅 `face/status`（只读）+ 发 `track/cmd`。
- **Step 3** UI：目标 cx/cy、跟踪开关。
- **Step 4** unmount 退订。

## 固件实现

- planned；实现时读取 face primary，驱动 gimbal/dog。
- 非本阶段。

## 验收

- [ ] 文档标明 planned；前端不误报已完成
