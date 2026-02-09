# deep-thumble 板级任务与优先级汇总

FreeRTOS 中**优先级数值越大，越优先被调度**；同优先级任务按时间片轮转。

下表为 deep-thumble 及相关公共组件中会创建的任务（仅列与当前板或通用音频/显示相关的）。

| 优先级 | 任务名 | 作用 | 创建位置 |
|--------|--------|------|----------|
| **8** | audio_input | 音频采集输入（麦克风 → 后续给 AFE feed） | audio_service.cc |
| **5** | user_main_loop | 用户主循环：10ms 周期 IMU、**TickDisplay**（消费 q_ai 显示）、500ms 占位等；需高于人脸任务 | deep_thumble.cc，`USER_MAIN_LOOP_TASK_PRIORITY` |
| **5** | CameraInitTask | （仅 ISP 相机时）相机初始化阶段后台拍 5s 帧并丢弃，完成后自删 | esp_video.cc |
| **4** | audio_output | 音频播放输出 | audio_service.cc |
| **3** | face_camera | 人脸管道采集：CaptureOnly → GetLastFrame → 入队 q_raw（后台辅助） | app_ai.cpp，`FACE_CAMERA_TASK_PRIORITY` |
| **2** | face_ai | 人脸管道 AI：q_raw → 检测/画框/透传 → q_ai（后台辅助；优先级 2 低于 3 以减少 task_wdt） | app_ai.cpp，`FACE_AI_TASK_PRIORITY` |
| **3** | face_delayed | 延迟 15s 后启动人脸管道，完成后自删 | deep_thumble.cc |
| **3** | audio_detection | AFE 唤醒检测（fetch 不及时会触发「Ringbuffer of AFE(FEED) is full」） | afe_wake_word.cc |
| **3** | audio_communication | 音频通信处理 | afe_audio_processor.cc |
| **3** | wifi_cfg_delay / acoustic_wifi | WiFi 配置延迟 / 声学配网，一次性 | wifi_board.cc |
| **2** | activation | 应用激活（网络/OTA 等），一次性 | application.cc |
| **2** | opus_codec | Opus 编解码 | audio_service.cc |
| **2** | encode_wake_word | 唤醒词 PCM→Opus 编码，按需创建 | afe_wake_word.cc |
| **1** | LVGL 任务 | 显示刷新、触摸等 | lcd_display.cc |

**说明：**

- 人脸管道（face_camera、face_ai）为**后台辅助能力**（检测/显示），已设为 3，与 audio_detection 同级，避免长时间占满 CPU 导致主循环无法消费 q_ai 或 AFE fetch 不及时。
- 主循环保持 5，优先于人脸与音频检测，保证 TickDisplay 和 IMU 等能及时执行。
- 若仍出现「Ringbuffer of AFE(FEED) is full」，可考虑将 audio_detection 提至 4，或再把人脸任务降至 2（会略降人脸帧率）。

---

## face_delayed 与 15s 延时

**目的**：避免与人脸管道争 PSRAM 导致 AFE/WakeNet 初始化失败。

- 应用启动后经历 starting → activating（网络/OTA/MQTT 等），约 **11s** 切到 **idle**。
- 到 idle 时 AFE/WakeNet（esp_sr）会分配约 **32KB PSRAM**；若此时人脸管道已在跑（帧池、队列、检测等已占 PSRAM），总余量不足会导致「Item psram alloc failed」，库内未检查 NULL 即 memcpy → 崩溃。
- **做法**：人脸管道不随板子一起启动，而是由 `DelayedFaceInitTask` 在 `vTaskDelay(FACE_RECOGNITION_DELAYED_START_MS)` 后再调用 `InitializeFaceRecognition()`，这样到 idle 时人脸尚未占 PSRAM，AFE 先拿到 32KB，之后再启人脸管道。

**15s 是否必要**：

- **必要的是「晚于 idle + AFE 初始化」**，不是必须 15s。文档里 idle 约 11s，15s 是**保守值**，留余量应对网络慢等情况。
- 若实际测下来激活常在 8–10s 内完成，可将 `FACE_RECOGNITION_DELAYED_START_MS` 改为 **10000–12000**，缩短前十几秒无预览的时间；若仍出现「Item psram alloc failed」，再改回 15000 或略增。
- 更稳妥的方式是**事件驱动**：在状态机切到 idle（或 AFE 初始化完成）后再启动人脸管道，而不是固定延时；需要改 `DelayedFaceInitTask` 为等待状态/事件后再调用 `InitializeFaceRecognition()`。

配置宏（config.h）：

- `USER_MAIN_LOOP_TASK_PRIORITY`：主循环优先级（建议 5）
- `FACE_CAMERA_TASK_PRIORITY`：人脸采集任务（建议 3）
- `FACE_AI_TASK_PRIORITY`：人脸 AI 任务（建议 2，低于 audio_detection 以减少长时间占用触发 task_wdt）
- `FACE_RECOGNITION_DELAYED_START_MS`：人脸管道延迟启动时间（ms）；目的见上文「face_delayed 与 15s 延时」，默认 15000，可酌情 10000–12000 缩短无预览时间

---

## task_wdt 与 face_ai 长耗时

**现象**：日志出现「Task watchdog got triggered - IDLE0 (CPU 0)」，当前运行任务为 `face_ai`，backtrace 指向 `esp_imgfx_color_convert_process`（YUYV→RGB565）或 `RunFaceDetectCore`。

**原因**：face_ai 单帧内连续执行 YUYV→RGB565、亮度统计、`detector->run()` 等，长时间不 yield，IDLE 任务得不到运行，看门狗超时。

**已做**：

- 去掉 face_detect_core 中 240→320×240 的 resize，直接 240×240 给 run()，减少一段长耗时。
- 去掉亮度门限逻辑（原 57k 像素循环）：暗场下模型本身难检出，直接跑检测即可，省耗时并减轻 task_wdt。
- face_ai 优先级降为 2。
- 在 YUYV 转换块结束后、`detector->run()` 结束后各加 `taskYIELD()`，把长流程拆成多段，让 IDLE 有机会运行。

**已采用**：deep-thumble 相机配置已改为 **OV3660 直接输出 RGB565**（config.json：`CONFIG_CAMERA_OV3660_DVP_RGB565_240X240_24FPS=y`），队列帧为 format=1，face_detect_core 与 task_face_display 不再做 YUYV→RGB565 转换，省去该段耗时并减轻 task_wdt。代码仍保留 format=3 分支以兼容 YUV422 配置。
