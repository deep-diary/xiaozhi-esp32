# deep-dog `face_ai`（人脸检测）

## 作用

- 在 **MJPEG 视频流** 同一套采图路径上，把 **RGB565 紧密帧** 送入独立 FreeRTOS 任务，调用 **`espressif/human_face_detect`**（与 `deep-thumble/app_ai` 同源思路）。
- 与 **`http-server` 解耦**：HTTP 仅调用 `face_ai_bridge.h`（送帧、开关、读 JSON），不在 `deep_dog_http_server.cc` 内写推理逻辑。
- **方案 B**：不在 JPEG 上画框；浏览器用 **Canvas** 叠在 `<img>` 上，**轮询** `GET /api/face` 取归一化框。

## 数据流

1. `dog_cam_http` 在 `Streaming` 模式下：`CaptureOnly` → `PackedRgb565FromFrame` →（可选）`DeepDogFaceAiSubmitFrameIfDue` → `image_to_jpeg` → `PublishJpeg`。
2. `dog_face_ai`：从深度 1 的队列取帧拷贝 → `DeepDogFaceDetectRun` → 更新线程安全的 `DeepDogFaceSnapshot`。
3. `GET /api/face`：`DeepDogFaceAiFormatJson` 返回 `enabled`、`feature_on`、`w/h`、`has_face`、`n`、`faces[]`（`x0..y1`、`cx/cy`、`score`，0～1 归一化）。
4. `POST /api/face_enable?enabled=0|1`：`DeepDogFaceAiSetEnabled`。

## 依赖与配置

- **组件**：`human_face_detect`、`esp-dl`（由 `main/CMakeLists.txt` 在 `BOARD_TYPE=deep-dog` 时加入 `PRIV_REQUIRES`）；模型与 Flash 占用见 `managed_components` 内组件说明。
- **板级宏**（`face_ai/face_ai_config.h`，由 `boards/deep-dog/config.h` include）：
  - `DEEP_DOG_FACE_AI_ENABLE`：总开关（0 为桩，无推理）。
  - `DEEP_DOG_FACE_AI_MIN_INTERVAL_MS`：送帧节流。
  - **`DEEP_DOG_FACE_DETECT_INPUT_RGB888`**（默认 **0**）：与 deep-thumble / esp-who 一致，直接送紧密 **RGB565** 给 `run()`，避免每帧再 malloc ~173KB RGB888（MJPEG 拉流时分配失败会静默变成 `n=0`）。若出现「脸在中间、框贴边」，可试 **`RGB565_SWAP=1`**，或临时改回 `INPUT_RGB888=1`（此时保持 `RGB565_SWAP=0`）。
  - **`DEEP_DOG_FACE_DETECT_RGB565_SWAP`**：仅在 **`INPUT_RGB888=0`** 时使用。
  - **`DEEP_DOG_FACE_DETECT_MSR_SCORE_THR` / `MNP_SCORE_THR`**（默认 **0.5**，与组件一致）：略高可压假框，但**切勿再提到 0.88 一类过高值**，否则真人/照片脸易漏检（`has_face=false` / `n=0`）。挡镜头主要靠暗场门控 + `MIN_BOX_PX`，而不是靠过高 score。
  - **`DEEP_DOG_FACE_DETECT_SKIP_UNIFORM_DARK`**：见下节「自检 0 框 vs 挡镜头仍有框」。

**与 esp-who / 正点原子例程**：本工程**未**依赖 esp-who 应用框架，只用了 **`espressif/human_face_detect` + `esp-dl`**。手册里的 `HumanFaceDetectMSR01` / `MNP01`、`infer` 属于**旧版 API**；当前组件为 **`HumanFaceDetect` + `run(img_t)`**，功能对应同一套 MSR+MNP 模型。若需完全手写管线，可再往下只调 `dl::Model`，但维护成本更高。
  - `DEEP_DOG_FACE_DETECT_BOX_SWAP_XY`：保持 **0**（`result_t.box` 已为 `[x0,y0,x1,y1]`，见 esp-dl `dl_detect_define.hpp`）。

## 与其它功能抢相机

当前仅在 **HTTP 采集模式为 Streaming** 且用户打开 **网页人脸开关** 时才会 `SubmitFrameIfDue`。语音拍照、Explain、触摸采图走其它路径，不经过本送帧逻辑；若未来统一相机锁，应在「独占相机」期间停止送帧并在本文档更新。

**网页提示**：控制页状态行的「狗初始化」对应电机姿态 `dog_initialized`，**与人脸模块是否就绪无关**；人脸开关打开且帧 `w/h` 非 0 即表示检测链路在跑。

## 全黑 / 暗场仍多框、框贴边（与 deep-thumble 一致时）

`boards/deep-thumble/docs/face-detection-root-cause.md` 已说明：在 **240×240 全黑** 下若 **RGB565 与 RGB888 都会出现多张脸**，则问题**不在** RGB565 大小端，而在 **MSR/MNP 量化输出、默认 score 阈值 0.5 过低、或与本工程编译优化/组件版本组合** 等「公共路径」；**不是** `human_face_detect` 分区绑错（当前 `sdkconfig` 为 `CONFIG_HUMAN_FACE_DETECT_MODEL_IN_FLASH_RODATA=y`，模型经 CMake `pack_espdl_models` 打进 `human_face_detect` 组件并链接进固件）。

本目录对策：

- 启动时 **黑图自检**（`DEEP_DOG_FACE_DETECT_BLACK_SELFTEST_LOG`）：串口见 `dog_face_det: black 240x240 self-test: ...`。
- **`DEEP_DOG_FACE_DETECT_MSR_SCORE_THR` / `MNP_SCORE_THR`**（默认 **0.5**）与略收紧 **NMS**；假框仍多时可略调高（如 0.6～0.7），但需用真人脸回归，避免再次漏检。
- **`DEEP_DOG_FACE_DETECT_MIN_BOX_PX`**（默认 20）滤掉贴边细条假框。
- 串口诊断：`dog_face_det: diag … raw=… filtered=…`（约每 40 次推理一次），可区分「模型真 0 框」与「min_box 滤空」。
- 若仍与官方例程差异大，应用 **同板烧录 esp-who `human_face_recognition`** 做 A/B，或锁定 **esp-dl / human_face_detect** 版本与官方示例一致。

### 自检 `black … 0 boxes` 但挡住镜头仍有框

二者不矛盾：

- 自检用的是 **`calloc` 全 0** 的 240×240 RGB565，且 **采样全 0 时不走「暗场门控」**，仍会跑一遍模型，故日志可验证阈值/模型在「数学全黑」上是否正常。
- 挡住镜头时，OV3660 常见为 **非零噪声、竖条/弱纹理**，整幅仍 **很暗、对比度不高**，与全 0 缓冲不同，网络仍可能出框；这 **不是** 单纯再调 `score` 阈值能完全对齐 thumble 体验时可消除的现象。

对策：**`DEEP_DOG_FACE_DETECT_SKIP_UNIFORM_DARK`**（默认 1）：稀疏采样绿通道，若 **均值 ≤ `UD_MAX_MEAN_G` 且 max−min ≤ `UD_MAX_RANGE_G`**，则 **直接返回无人脸、不跑推理**。纯 0 采样不触发，以免自检失效。挡镜头仍出框时可略 **增大 `UD_MAX_RANGE_G`**（噪声大）或 **减小 `UD_MAX_MEAN_G`**（更激进）；极暗环境真人脸若被误杀可 **置 0 关闭门控** 或放宽阈值。

## 二期

本地识别可在此目录增加 `face_recognize.*`，仍通过 bridge 暴露只读状态，不侵入 `http-server` 推理代码。
