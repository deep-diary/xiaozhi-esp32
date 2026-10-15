# V-S08 · RTSP/H264 + 人脸并发 WDT 缓解

| 项 | 内容 |
|----|------|
| 路线图 ID | **V-S08** |
| 依赖 | [C02](../client/C02-device-push-stream.md) · [S04](./S04-local-face-numeric-id.md) · [04-face MQTT](../../mqtt/modules/04-face.md) |
| 代码落点 | `face_ai/face_ai_runtime.cc` · `face_ai_config.h` · `vision/vision_frame_hub.cc` · `vision/stream_audio_gate.*` · `mqtt/modules/stream_mqtt.cc` |
| 状态 | **已落地（间隔下限 + 语音互斥 + 5min 自动停 + face_facedb）**；帧零拷贝仍为后续 POC |

## 1. 背景

`vision_hub`（H264 软编，PSRAM 栈 ~48KB）与 `dog_face_ai`（检测+识别，internal 栈 12KB）并行时，CPU 长时间占满曾触发 **task WDT**（`IDLE0` + `dog_face_ai` / `vision_hub`）。

internal RAM 瓶颈在 **固定 task 栈 + WiFi/MQTT**，与注册人脸人数无关（embedding ~2KB/条，PSRAM）。

## 2. 目标

- RTSP 推流 + 人脸检测 **可并存**，但默认 **降低人脸送帧频率**。
- 不合并 `dog_face_ai` 与 `vision_hub` 为单 task（避免阻塞与 WDT 恶化）。
- 可观测：`device/status.tasks[]` 提供 `stack_hwm` 审计。

## 3. 实现

| 宏 | 默认 | 行为 |
|----|------|------|
| `DEEP_DOG_FACE_AI_DURING_RTSP` | 1 | 推流时仍送帧 |
| `DEEP_DOG_FACE_AI_RTSP_MIN_INTERVAL_MS` | **2000** | RTSP 模式 active 时，送帧间隔 **max(user, 2000ms)** |
| `DEEP_DOG_STREAM_RTSP_MAX_S` | **300** | RTSP 推流最长 5 分钟，超时自动 `stop` |
| `stream_audio_gate` | — | RTSP 开：停唤醒+语音；关：恢复待机唤醒 |

`VisionFrameHub::SetPublishMode(RtspPush)` → `DeepDogFaceAiSetVisionRtspActive(true)` + `DeepDogStreamAudioGateSetRtspActive(true)`。

RTSP 推流期间 **暂停 AFE**（`EnableWakeWordDetection(false)` + `EnableVoiceProcessing(false)`），消除 `AFE Ringbuffer full`；推流结束或 5min 超时后恢复唤醒。

用户仍可通过 MQTT `face/cmd.detect_interval_ms` 抬高间隔；低于下限时固件自动夹紧。推流期间识别最小间隔临时抬高至 **4000ms**（不写 NVS）。

### 3.1 facedb Flash 与 PSRAM 栈（`face_facedb`）

`dog_face_ai` **优先 PSRAM 栈**（模型推理省 internal）。esp-dl **facedb** 的 `enroll` / `delete` / `clear` 经 FAT 写 Flash，触发 `esp_task_stack_is_sane_cache_disabled()` — **禁止**在 PSRAM 栈任务内调用。

| 任务 | 栈 | 职责 |
|------|-----|------|
| `dog_face_ai` | PSRAM | 检测、feat 推理、`query_feat`（纯内存） |
| `face_facedb` | **internal** | `enroll_feat` / `delete_feat` / `clear_all_feats` |
| `face_persist` | internal | NVS meta |
| `dog_immich` | **PSRAM**（优先） | HTTP；NVS 经 `face_persist` |

lazy-start 顺序：`face_persist` → **`face_facedb`** → `dog_face_ai` → 模型加载。`RecognizeOneFace` 在 PSRAM 任务内算 feat，再 **同步**投递 `face_facedb` 写 facedb。

## 4. 验收

- [ ] RTSP 推流期间无（或极少）`AFE: Ringbuffer ... full`
- [ ] 推流 5 分钟后 `stream/status` → `idle`，`error=auto_stop_timeout`，唤醒恢复
- [ ] `stream/status.voice_paused` 推流中为 `true`
- [ ] facedb enroll / delete / clear_db 不触发 `esp_task_stack_is_sane_cache_disabled`（`face_facedb` internal 栈）
- [ ] 推流时串口可见送帧间隔 ≥ `RTSP_MIN_INTERVAL_MS`（或用户更大值）
- [ ] `device/status.mem.internal.min` 不低于健康阈值（见 01-device `low_internal_heap`）
- [ ] `scripts/deep_dog/deep_dog_device_audit.py` 可输出 `tasks[]` 与 `face_summary`

## 5. 后续 POC（未做）

- VisionFrameHub → Face AI **零拷贝** ring buffer（减 PSRAM 115KB/帧拷贝）
- H264 仅推流时启动 hub 深栈 task
- `DEEP_DOG_FACE_AI_DURING_RTSP=0` 剖面开关（推流时只检测框走 MQTT overlay 节流）

## 6. 运维建议

1. 推流页联调：`detect_interval_ms` ≥ 1000 或 `recognize_enabled=false`
2. 避免 BT + Face AI + TLS OTA 同开（internal 不足）
3. 用 audit 脚本对比 `mem.internal` 与 `tasks[].stack_hwm`
4. internal 系统性优化见 **[S09 internal SRAM](./S09-internal-sram-optimization.md)**（启动错峰、栈 trim、RESERVE 等）
