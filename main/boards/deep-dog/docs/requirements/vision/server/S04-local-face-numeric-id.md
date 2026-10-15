# V-S04 · 本地人脸识别：自动数字 ID（下一步）

| 项 | 内容 |
|----|------|
| 路线图 ID | **V-S04** |
| 优先级 | P1 |
| 依赖 | [S03](./S03-http-face-overlay.md) |
| 下一切片 | [S05 Immich 真名](./S05-immich-real-name.md) |
| 代码落点 | `face_ai/face_recognize.*` + NVS；不在 httpd 回调推理 |
| 基线状态 | **已落地（检测+数字 ID；待你方用多人/重启再验收）** |

## 1. 背景

S03 仅有检测框。本阶段在 **HTTP 服务器路径**上打通本地识别闭环：**不调用 Immich**。未入库的人脸自动建档，用递增**数字 ID**标识。

## 2. 目标

- 检测开启时：有脸 → embedding → 会话去重（约 **5s**）→ NVS 1:N。
- **未命中**：自动分配 `local_id = 1,2,3…`，显示名可用 `#1` / `id:1`，写入 NVS。
- 控制页 / `/api/face` 可见当前 `local_id`。

## 3. 范围

**包含**：embedding、5s 会话缓存、NVS（**≤32** feat 槽位，canonical 人数通常更少）、满库 **LRU 淘汰最久未见 embedding 槽**、自动建档、API/UI 数字 ID、alias 合并（V-S07）。  
**不包含**：Immich（S05）、Kiosk（P01）、跟脸、MediaMTX 推流（C02）。

## 4. 决策流

```text
有脸 → embedding
  → 会话窗口内相似？ → 复用 local_id（不新建）
  → NVS 1:N 命中？ → 复用 local_id
  → 否则 → 分配新数字 ID → 写 NVS → 返回
```

## 5. NVS 条目（本阶段）

| 字段 | 说明 |
|------|------|
| `local_id` | 正整数，设备内唯一 |
| `display_name` | 默认 `#<id>`，S05 可改为真名 |
| `embedding` | 定长向量 |
| `updated_at` | 更新时间 |
| `immich_person_id` | 本阶段可空，留给 S05 |

## 6. HTTP

| 项 | 约定 |
|----|------|
| 开关 | 识别依赖 `face_enable`（检测开） |
| `GET /api/face` | 增加 `local_id`、`display_name`、`recognize_source`（`session`/`nvs`/`enrolled`/`none`） |
| UI | 预览旁显示数字 ID |

## 7. 功能需求

| ID | 需求 |
|----|------|
| NID-01 | 未保存人脸自动建档并分配新数字 ID |
| NID-02 | 同人 5s 内 / NVS 命中保持同一 ID |
| NID-03 | **禁止**调用 Immich |
| NID-04 | 断电后 NVS 仍可认出已存成员 |
| NID-05 | 任务隔离；可关检测则停识别 |
| NID-06 | 多人脸：同一帧最多识别 **N** 张（默认 4，按 score）；每张可有独立 `local_id` |

## 8. 验收标准（关闭本切片）

- [ ] 对着**不同的人**，页面或 `/api/face` 显示**不同的数字 ID**
- [ ] 同一人连续出镜，ID 稳定不变（会话或 NVS）
- [ ] 串口/日志无 Immich HTTP 请求
- [ ] 重启后已建档成员仍回到原 `local_id`

## 9. 与后续

- **S05**：为已有 `local_id` 绑定 Immich 真名。  
- **C02**：推流复用本阶段同一套 `face_ai` 状态机。
