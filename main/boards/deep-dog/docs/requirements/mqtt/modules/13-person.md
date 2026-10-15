# 13 · person（并入 face）

| 项 | 内容 |
|----|------|
| module_id | `person`（Topic 仍可用） |
| capabilities | **不单独出入口卡**；随 `face` |
| 路由 | `/modules/person` → **redirect** → `/modules/face` |
| 契约 | `person/active` 草案；产品见 [P01](../../vision/product/P01-kiosk-personalization.md) |
| 主文档 | **[04-face](./04-face.md)**（Immich 轮播、打招呼、身份） |

## 说明

原独立「人物」卡与 Face 内容重合，已合并。前端只渲染 Face 入口；需要身份 retain 事件时仍可订：

```json
{ "local_id": 2, "display_name": "张三", "immich_person_id": "uuid-optional", "ts": 1710000000 }
```

## 验收

- [ ] 无独立 Person 入口卡
- [ ] `/modules/person` redirect 到 face
- [ ] Face 页可完成轮播 / 打招呼主路径
