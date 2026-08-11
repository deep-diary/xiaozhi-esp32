# P02 · 主动招呼 + E2E 模拟联调

| 项 | 内容 |
|----|------|
| ID | V-P02 |
| 依赖 | S05 · S07 · 04-face |
| 状态 | 实现中 |

## 目标

- 识别到**已命名**熟人且 `now - last_seen_at >= greet_gap_sec` → `WakeWordInvoke("face_greet")`
- `device/status.current_speaker` 记录当前对话者
- MCP `self.face.get_identity` 供云端/用户问「你知道我是谁吗」
- **联调**：`face/cmd` / MCP `simulate_greet` 注入姓名（如葛维冬）并触发唤醒，无需摄像头

## MQTT · `face/cmd` 扩展

| action | 字段 | 说明 |
|--------|------|------|
| `simulate_greet` | `name` 或 `display_name`，`local_id?` | E2E 测试 |
| `clear_speaker` | — | 清空 current_speaker |
| `set_greet_config` | `greet_enabled`, `greet_gap_sec` (10–86400) | NVS 持久 |

也可在 payload 顶层发 `greet_enabled` / `greet_gap_sec`（与 enabled 同级）。

## MCP

- `self.face.get_identity` — 只读 JSON
- `self.face.simulate_greet` — 同 MQTT
- `self.face.clear_speaker`
- `self.face.set_greet_config`

## 云端 prompt（粘贴到小智服务端）

当 wake word 为 **`face_greet`** 时：

1. 必须先调用 `self.face.get_identity`
2. 若 `present` 且 `display_name` 非空 → 首句 TTS：「{display_name}你好呀，好久不见！」
3. 用户问「你知道我是谁吗」→ 同上 MCP，答「我猜你是 {display_name}，我刚才认出你来了」

## 验收

- MQTT：`{"action":"simulate_greet","name":"葛维冬"}` → log `simulate_greet ok=1` + `Wake word detected: face_greet`
- `device/status.current_speaker.display_name` = 葛维冬
- enroll 新人 `last_seen_at=0`（UI「从未」）
