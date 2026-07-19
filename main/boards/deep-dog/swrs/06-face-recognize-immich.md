# 06 · Immich 人脸识别与结果回传

| 项 | 内容 |
|----|------|
| 优先级 | P1 |
| 依赖 | [05 人脸检测上传](./05-face-detect-upload.md)、Immich 实例可用、DeepDiary 后台 |
| 代码落点 | **以后台服务为主**；设备仅接收 MQTT `face/result` |
| 验收 | 上传人脸后，设备与前端都能拿到人物身份（或 unknown） |

## 1. 背景

设备只负责「有脸 + 传图」。身份识别复用已有 **Immich** 人物库，由后台调用，避免在 ESP 上维护人脸库与 API Key。

## 2. 目标

- 后台收到人脸图后调用 Immich，得到 `person`（或未识别）。
- 结果经 MQTT 回传设备；同时发 `person/active` 给前端（[07]）。
- 为对话个性化提供「当前在场人物」上下文。

## 3. 范围

**包含**

- 后台：收图 → Immich → 规范化人物 DTO → MQTT 发布
- 设备：订阅 `face/result`，缓存「当前人物」供后续对话注入
- 未识别、低置信度、超时等错误语义

**不包含**

- Immich 相册管理 UI、手动标注流程（运维侧已有则复用）
- Kiosk 轮播与档案文案（[07]）
- 设备本地 1:N 识别

## 4. 推荐链路

```text
POST /faces (JPEG)
    → 后台暂存
    → Immich：搜索 / 分配 person（以实际 Immich API 为准）
    → 查 DeepDiary 人物档案（person_id ↔ 档案）
    → MQTT face/result → 设备
    → MQTT person/active → 前端 / Kiosk
```

**为何不设备直连 Immich**

- API Key 不宜进固件
- Immich API 变更与重试逻辑放后台更合适
- 便于统一审计与限流

## 5. 协议草案

**回传设备** `.../face/result`

```json
{
  "upload_id": "…",
  "status": "matched" | "unknown" | "error",
  "person_id": "immich-or-local-id",
  "name": "小明",
  "confidence": 0.87,
  "profile_id": "deepdiary-profile-uuid",
  "ts": 1710000000,
  "error": null
}
```

**通知前端** `.../person/active`（也可做全局 `deepdiary/kiosk/person/active`）

```json
{
  "device_id": "…",
  "person_id": "…",
  "name": "小明",
  "profile_id": "…",
  "album_hint": "optional-album-id",
  "ts": 1710000000
}
```

设备行为建议：

- 缓存最近一次 `matched` 结果，TTL 如 5–15 分钟或直到 `unknown`/新人物覆盖。
- `error` 不覆盖有效缓存，仅打日志。

## 6. 功能需求

| ID | 需求 | 说明 |
|----|------|------|
| REC-01 | Immich 识别 | 后台对上传图完成识别调用 |
| REC-02 | 映射档案 | Immich person ↔ DeepDiary 个人档案；无档案时仍返回 name/person_id |
| REC-03 | 回传设备 | 发布 `face/result`，设备可解析并缓存 |
| REC-04 | 通知前端 | 发布 `person/active` |
| REC-05 | 限流 | 同一 device 识别 QPS / 冷却与 [05] 去抖配合 |
| REC-06 | 失败可见 | Immich 超时/失败返回 `status=error`，可观测 |

## 7. 验收标准

- [ ] 用已知人物脸图走通：后台日志有 Immich 命中，MQTT 收到正确 `name`
- [ ] 陌生人：`unknown`，前端不误切到错误相册
- [ ] 设备在收到 `matched` 后，日志或状态可查询到当前人物
- [ ] Immich 宕机时系统降级为 `error`/`unknown`，不拖垮上传接口

## 8. 与对话个性化的衔接（预留）

后续在对话请求中附带 `profile_id` / 人物摘要（由应用层或 MCP 注入）。具体 prompt 拼装见 [07](./07-kiosk-personalization.md)，本需求只保证**身份事件可靠到达**。

## 9. 开放问题

1. Immich 侧具体用哪个 API（人脸搜索 vs 已有 person embedding）？需对照当前 Immich 版本写一页对接说明。
2. 多人同时在场时，`person/active` 是单人还是列表？P1 建议单人（最大脸）。
