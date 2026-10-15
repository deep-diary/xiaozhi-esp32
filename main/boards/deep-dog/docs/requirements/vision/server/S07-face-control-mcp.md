# V-S07 · 人脸控制 MCP + FaceControl 统一层

| 项 | 内容 |
|----|------|
| 路线图 ID | **V-S07** |
| 依赖 | [S04](./S04-local-face-numeric-id.md) · [S05](./S05-immich-real-name.md) · [04-face MQTT](../../mqtt/modules/04-face.md) · [WS-01](../../ws-mcp/01-local-mcp-bridge.md) |
| 代码落点 | `face_ai/face_control.*` · `face_ai/face_mcp.cc` · `mqtt/modules/face_mqtt.cc` |

## 1. 目标

- **FaceControl** 统一 API：MQTT / WS MCP / HTTP 共用。
- 检测与识别**分拆开关**；人脸库 CRUD；alias 合并；`face/registry` retain。
- MCP **聚合工具**（≤3），避免小智云端 32 tool 上限膨胀。

## 2. FaceControl API

| API | 说明 |
|-----|------|
| `SetDetectionEnabled(bool)` | 关则停采帧推理 |
| `SetRecognitionEnabled(bool)` | 关则只检测不 enroll/Immich |
| `ClearAll()` | facedb + NVS + session |
| `DeleteOne(local_id)` | 删单条 feat + meta |
| `Rename(local_id, name)` | 写 NVS display_name（canonical） |
| `MergeAlias(source, target)` | 保留 source embedding，meta.canonical_id=target |
| `ListEnrolled()` | canonical 列表 + aliases |
| `FormatRegistryJson()` | MQTT retain 载荷 |
| `RefreshImmichName(local_id)` | 对已有 asset_id 立即重 poll |

## 3. MCP 工具（聚合）

| 工具 | 参数 |
|------|------|
| `self.face.set_mode` | `detect`, `recognize`, `pipeline?`, `interval_ms?` |
| `self.face.manage` | `action`: clear_all \| delete \| rename \| merge \| refresh_immich；`local_id?`, `target_id?`, `name?` |
| `self.face.list` | `include_live?` → enrolled + 可选 live snapshot |

注册：`esp_sparkbot_board.cc` `#if DEEP_DOG_FACE_AI_ENABLE`。

## 4. 存储（meta_ver=3）

| 字段 | 说明 |
|------|------|
| `immich_asset_id` | upload 即写；延迟 poll 绑定 |
| `canonical_id` | 0=canonical；非 0=alias 指向 |
| `name_pending` | 1=待 Immich 命名 |

`DEEP_DOG_FACE_RECOG_MAX` = **32**（`FaceMeta` 静态 BSS ~4.5KiB；满库按 embedding 槽 `last_seen_at` LRU 淘汰）。

`face/registry` 增 `feat_count`、`max_count`；entries 增 `last_seen_at`（上次识别见面，Unix 秒）。

## 5. 验收

- [ ] MCP/ MQTT / WS 均可 set_mode、list、manage
- [ ] alias merge 后侧脸命中 source feat 显示 canonical 名
- [ ] asset_id 持久化；Immich 稍后命名后绑定成功
- [ ] `face/registry` retain 与 list 一致
