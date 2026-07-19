# V-S05 · Immich 真名绑定到本地数字 ID

| 项 | 内容 |
|----|------|
| 路线图 ID | **V-S05** |
| 优先级 | P1 |
| 依赖 | [S04](./S04-local-face-numeric-id.md)、[infra Immich](../infra.md) |
| 下一切片 | [C02 设备推流](../client/C02-device-push-stream.md)（或先做 C01） |
| 代码落点 | `face_ai/` Immich 客户端；不在 httpd 回调上传 |
| 基线状态 | 待办 |

## 1. 背景

S04 已用数字 ID 区分人。本阶段在**已有或新建 local_id** 上，按需直连**局域网 Immich** 取真名并回写 NVS。失败时仍显示数字 ID。

## 2. Immich 约束

无同步「上传即返回姓名」API。契约：

```text
裁剪 JPEG → 上传临时 asset → 轮询 faces/person → name + person_id
```

延迟秒级；须配合 S04 去重，禁止 1Hz 狂刷。详见 [infra](../infra.md)。

## 3. 何时调用

仅当编排判定需要远程命名时，例如：

- 新 enrolled 的 `local_id` 尚无 `immich_person_id` / 真名；或  
- 用户显式「刷新姓名」。

会话/NVS 已有真名则**不调** Immich。

## 4. 结果与 UI

```json
{
  "status": "matched" | "unknown" | "error",
  "local_id": 2,
  "person_id": "immich-uuid-or-null",
  "name": "小明",
  "source": "immich",
  "ts": 1710000000
}
```

- `matched`：写 NVS（`display_name`、`immich_person_id`）；`/api/face` 显示真名。  
- `unknown`/`error`：保留数字 ID 显示；退避，不覆盖有效真名缓存。

Key：NVS；禁止 git 明文；禁止生产只读 MCP Key 做上传。默认 `http://192.168.31.25:2283/api`。

## 5. 功能需求

| ID | 需求 |
|----|------|
| NAM-01 | 仅按上节门控调用 Immich |
| NAM-02 | 异步不阻塞检测/MJPEG/狗控 |
| NAM-03 | 成功绑定 local_id ↔ 真名 |
| NAM-04 | 失败降级为数字 ID |
| NAM-05 | 可观测（upload/poll/result 日志） |

## 6. 验收

- [ ] Immich 已命名人物：可得正确真名（允许数秒延迟）
- [ ] Immich 宕机：仍显示 S04 数字 ID，推流/检测可用
- [ ] 同人 ≥5s：Immich 调用符合去重（至多一次远程命名）
- [ ] 仓库无 API Key 明文

## 7. 不包含

DeepDiary 后台中转主路径；Kiosk 正文（[P01](../product/P01-kiosk-personalization.md)）。
