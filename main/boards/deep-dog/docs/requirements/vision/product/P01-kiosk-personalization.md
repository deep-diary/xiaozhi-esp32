# V-P01 · Kiosk 轮播与对话个性化

| 项 | 内容 |
|----|------|
| 路线图 ID | **V-P01** |
| 优先级 | P2 |
| 依赖 | [S05](../server/S05-immich-real-name.md)（真名）或至少 [S04](../server/S04-local-face-numeric-id.md) |
| 代码落点 | 前端 / 档案服务为主；身份来自设备 |

## 目标

- 订阅 `person/active` 或轮询设备 `/api/face`，Kiosk 轮播该人物相册。
- 对话注入个人档案；无人物时用默认人设。

## 身份来源

设备侧 `local_id` / 真名 / `immich_person_id`（S04/S05）；**不**要求再经 DeepDiary 后台上传识别。

## 验收

- [ ] 已知人物出镜 → Kiosk 切换轮播
- [ ] TTL/离开 → 回默认
- [ ] unknown → 不套用错误档案
