# 13 · person（当前人物）

| 项 | 内容 |
|----|------|
| module_id | `person` |
| capabilities | 可随 `face` 或独立 `person`（产品定；默认随 P01） |
| 路由建议 | `/device/:deviceId/modules/person` |
| 契约 | **reserved / planned** |
| YAML | `person/active` |
| 产品 | [P01 Kiosk](../../vision/product/P01-kiosk-personalization.md) |

## 入口卡文案

- 标题：人物  
- 说明：当前识别人物（规划中）  

## 详情页目标

展示 `local_id` / `display_name`（Kiosk 用）。

## Topic

| Topic | 方向 | QoS | retain |
|-------|------|-----|--------|
| `person/active` | ↑ | 0 | true |

## 样例 JSON

```json
{ "local_id": 2, "display_name": "葛维冬", "ts": 1710000000 }
```

以 [YAML](../protocol/deep-dog-mqtt.yml) 为准。

## Steps（前端）

- **Step 1** planned：默认隐藏入口卡，或显示「即将推出」。
- **Step 2** 若开放：订阅 `person/active`，渲染姓名/ID。
- **Step 3** 无下行 cmd（v0.1）。
- **Step 4** unmount 退订。

## 固件实现

- P01；可由 face 识别结果派生发布。

## 验收

- [ ] 标明 planned，不与 face 详情页职责混淆
