# P02 · 主动招呼 + E2E 模拟联调

| 项 | 内容 |
|----|------|
| ID | V-P02 |
| 依赖 | S05 · S07 · 04-face |
| 状态 | 实现中 |

## 目标

- 识别到**已命名**熟人且 `now - last_seen_at >= greet_gap_sec` → `WakeWordInvoke("face_greet:<display_name>")`
- `device/status.current_speaker` 记录当前对话者
- MCP `self.face.get_identity` 供云端/用户问「你知道我是谁吗」
- **联调**：`face/cmd` / MCP `simulate_greet` 注入姓名（如葛维冬）并触发唤醒，无需摄像头

## MQTT · `face/cmd` 扩展

| action | 字段 | 说明 |
|--------|------|------|
| `simulate_greet` | `name` 或 `display_name`，`local_id?` | E2E 测试 |
| `clear_speaker` | — | 清空 current_speaker |
| `set_greet_config` | `greet_enabled`, `greet_gap_sec` (10–86400) | NVS 持久；联调默认 **1800**（30min，`DEEP_DOG_FACE_GREET_DEFAULT_GAP_SEC`） |

也可在 payload 顶层发 `greet_enabled` / `greet_gap_sec`（与 enabled 同级）。

## MCP

- `self.face.get_identity` — 只读 JSON
- `self.face.simulate_greet` — 同 MQTT
- `self.face.clear_speaker`
- `self.face.set_greet_config`

## 云端 wake `text` 格式（自建服解析）

设备 `SendWakeWordDetected` 的 `text` 字段：

```text
face_greet:<display_name>
```

- 分隔符：**ASCII 冒号** `:`（非全角 `：`）
- 示例：`face_greet:葛维冬`
- 无姓名时（不应出现在识别路径）：仅 `face_greet`
- 自建服：`split(':', 1)` → `kind=face_greet`，`name=葛维冬`；仍建议调 `self.face.get_identity` 校验

## 云端 prompt（粘贴到小智 / 自建服务端）

当 `listen.detect.text` **以 `face_greet:` 开头**（或 kind=`face_greet`）时：

1. 必须先调用 `self.face.get_identity`
2. 若 `present` 且 `display_name` 非空 → 首句 TTS：「{display_name}你好呀，好久不见！」
3. 用户问「你知道我是谁吗」→ 同上 MCP，答「我猜你是 {display_name}，我刚才认出你来了」

## 验收

- MQTT：`{"action":"simulate_greet","name":"葛维冬"}` → log `simulate_greet ok=1` + `Wake word detected: face_greet:葛维冬`
- `device/status.current_speaker.display_name` = 葛维冬
- enroll 新人成功后写入 `last_seen_at`（= 首次注册时刻，SNTP 同步前提下 UI 不再显示「从未」）
- init 从 facedb 补 orphan 槽 **不**写 `last_seen_at`
