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
| `dog_face_ai` / `dog_immich` | **8 KB / 10 KB**（config 可调） | internal；hwm 审计见 §2 |
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

## 7. S06 §9 双分辨率压测对比（2026-08-13）

| 剖面 | CAN | MOTOR | HANDLE | 采集 | RTSP 编码 | internal.free | internal.min | task_count | WDT | 备注 |
|------|-----|-------|--------|------|-----------|---------------|--------------|------------|-----|------|
| **瘦剖面（默认）** | 0 | 0 | 0 | 640×480 | 320×240 | 22914 | **2542** | 21 | 无 | face+RTSP 并发；`dog_face_ai` 栈 7680 |
| +handle | 0 | 0 | 1 | 640×480 | 320×240 | 35890 | 24954 | 20 | 无 | idle 无 face；`capabilities.handle=true` |
| +CAN+电机（源码=1） | 1* | 1* | 0 | 640×480 | 320×240 | 17642 | 2162 | 22 | 无 | *`EXT_PIN=LED` 钳位，CAN/电机未实际编入 |
| 240² 回退 | 0 | 0 | 0 | 240×240 | 240×240 | — | — | — | — | §8.1 应急 |

**RTSP 验收**：ffmpeg 探测 `h264 320x240 @5fps`；串口 `H264 push ok capture=640x480 encode=320x240`。

**face 验收**：MQTT `face/status` `w/h=640/480`；`stream_face_stress_test.py` 7/7 PASS。

**栈调整**：VGA 下 `largest_int≈7936` 不足以 `xTaskCreate(...,8192)` → `DEEP_DOG_FACE_AI_TASK_STACK=7680`。

采集方式：MCP `self.board.diagnostics` / MQTT `device/status`；RTSP：`ffmpeg -i rtsp://...`。

## 8. 开机人脸 + 内存可观测（2026-08-13）

**固件配置**

| 宏 | 值 | 说明 |
|----|-----|------|
| `DEEP_DOG_FACE_AI_DEFAULT_ENABLED` | **1** | WiFi IP 就绪后自动 `DeepDogFaceAiRuntimeStart()`（检测+识别） |
| `DEEP_DOG_FACE_GREET_DEFAULT_GAP_SEC` | **1800** | 主动招呼 30min |
| `DEEP_DOG_FACE_GREET_BOOT_APPLY` | **1** | 开机写 NVS，覆盖旧 10s 间隔 |

**串口日志（验收）**

- `dog_face_greet: init enabled=1 gap=1800s`
- `dog_mem_rpt: memory report [boot_baseline]`（Face 启动前）
- `dog_face_ai: runtime started ...`
- `dog_mem_rpt: memory report [face_ready]`（含 Top 任务 stack_hwm）

**MQTT `device/status` 新字段**

- `mem.*.{largest_free,used}`
- `tasks[]` 增 `stack_domain` / `stack_used_est`；按 `stack_hwm` 升序

**预期 internal 基线（face-on，240² 瘦剖面，无 RTSP）**

| 阶段 | internal.free | internal.min | 判定 |
|------|---------------|--------------|------|
| boot_baseline | ~40–50 KB | ~35–40 KB | 正常 |
| face_ready | ~15–25 KB | **≥7 KB** 可接受 | face+immich task 占 ~15KB 栈 |
| face+RTSP 并发 | ~18–23 KB | **2.5–7 KB** | 紧张但可运行；min&lt;4KB 需关注 |

**全局变量与 SRAM**

- `.data`/`.bss` 静态全局在链接时占 internal DRAM，**不计入** `heap_caps total`。
- `total≈458KB` 为 **heap 池**；芯片 internal ≈512KB，差值≈静态段+RTOS+WiFi 保留。
- 运行时审计靠 `tasks[]` + heap free/min；静态符号需 `idf.py size-components`。

**刷机**：`idf.py build` + `flash` ✅（2026-08-13）；串口验收通过。

**实机采样（2026-08-13 刷机后 cold boot）**

| 阶段 | internal.free | internal.min | internal.largest | psram.free |
|------|---------------|--------------|------------------|------------|
| boot_baseline | **79,454** | 79,230 | 57,344 | 7,927,388 |
| face_ready | **59,270** | **44,870** | 47,104 | 5,335,600 |

- `dog_face_greet: init enabled=1 gap=1800s` ✅
- `dog_face_ai: runtime started ... recog=1 immich=1` ✅
- 无 WDT/panic ✅
- face_ready 后 internal.min≈45KB，**显著优于**旧剖面（~7KB / ~4KB），因未并发 RTSP 且 cold boot

**需求出处**：[04-face](./mqtt/modules/04-face.md)、[P02](./vision/product/P02-proactive-face-greet.md)、[01-device](./mqtt/modules/01-device.md)
