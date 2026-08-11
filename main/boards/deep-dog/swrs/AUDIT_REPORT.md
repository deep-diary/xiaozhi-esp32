# deep-dog 设备审计报告

生成时间：2026-08-11（计划执行）

设备 IP：`192.168.31.211` · WS MCP：`ws://192.168.31.211:8080/ws`

## 1. 人脸库（WS MCP `self.face.list`）

| 项 | 值 |
|----|-----|
| **canonical 人数** | **4** |
| **alias embedding 数** | **8**（均挂在 local_id=1） |
| **facedb feat 槽估计** | **12**（4 canonical + 8 alias） |

| local_id | display_name | 备注 |
|----------|--------------|------|
| 1 | 葛维冬 | Immich 已绑；aliases: 2–9 |
| 15 | 韩莉 | Immich 已绑 |
| 20 | #20 | Immich name_pending |
| 24 | #24 | 占位名 |

**结论**：每人 **不是 40KB internal**；每条 embedding ≈ **2KB（PSRAM/flash）**。internal 压力来自 **task 栈**（如 `dog_face_ai` 12KB、`opus_codec` 24KB）。

## 2. 内存与 Task 实测（WS `self.board.diagnostics`，2026-08-11 刷机后）

| 项 | 开机约 30s（face 未启） | face 启用后 |
|----|-------------------------|-------------|
| `mem.internal.free` | **47,126 B** | **16,878 B** |
| `mem.internal.min` | **37,042 B** | **7,514 B**（触发 `low_internal_heap`） |
| `mem.psram.free` | **7,381,088 B** | 充裕 |
| `task_count` | **20** | **22**（+`dog_face_ai` +`dog_immich`） |

**栈高水位 Top（face 未启时）**：

| 任务 | stack_hwm | 说明 |
|------|-----------|------|
| `vision_hub` | **48,428 B** | PSRAM 栈；占 hwm 总和大头 |
| `opus_codec` | **16,756 B** | internal |
| `dog_immich` | **6,580 B** | internal（face 启用后） |
| `dog_face_ai` | **4,352 B** | internal（face 启用后；分配 12KB） |
| `esp_timer` | 5,472 B | IMU 等周期回调分发 |
| `audio_input` | 4,532 B | |

**结论**：internal 瓶颈在 **语音 + MQTT + 未启 face 时已仅 ~37KB min**；与注册人数无关。每人 embedding ≈ 2KB（PSRAM）。

## 3. MQTT 快照

- 局域网 broker 可连，但设备 **实时 `device/status` 未在采样窗口出现**（可能 MQTT 客户端未连上或 device_id 不匹配 retain）。
- 联调请用 **`self.board.diagnostics`** / **`self.face.list`**（WS MCP）；刷机后需 `self.face.set_mode detect=true` 才加载 facedb 列表。

## 4. Task / RAM 设计要点（摘要）

| 任务 | 栈 | 域 |
|------|-----|-----|
| `vision_hub` | 48 KB | PSRAM |
| `dog_face_ai` / `dog_immich` | 各 12 KB | internal（不可迁） |
| `opus_codec` | 24 KB | internal |
| IMU | **无独立 task** | `esp_timer` 10ms |

**不建议**全局合并为 10ms 主调度 task；重 CPU 任务保持独立。见 SWRS V-S08。

## 5. 本次固件改动

- `device/status` 增加 `tasks[]` / `task_count`（需 `CONFIG_FREERTOS_USE_TRACE_FACILITY`）
- MCP **`self.board.diagnostics`**：内存 + tasks（MQTT 离线时可审计）
- RTSP 推流时人脸送帧间隔下限 `DEEP_DOG_FACE_AI_RTSP_MIN_INTERVAL_MS=1500`
- 审计脚本：`scripts/deep_dog/deep_dog_device_audit.py`

## 6. 需求出处

- [S08 RTSP+人脸 WDT](../main/boards/deep-dog/swrs/vision/server/S08-rtsp-face-wdt-mitigation.md)
- [M02 MCP 控制面](../main/boards/deep-dog/swrs/mqtt/M02-mcp-call-control-plane.md)
- [01-device status 字段](../main/boards/deep-dog/swrs/mqtt/modules/01-device.md)
