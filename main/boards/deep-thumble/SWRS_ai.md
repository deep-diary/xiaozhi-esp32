# 双队列人脸检测架构（已实现）

## 目标与约束

- **目标**：相机按节奏取帧入队；AI 独立消费、检测、画框后入队；显示端从队列取“已画框”帧刷新屏幕，职责解耦、流水清晰。
- **约束**：当前 `main/boards/common/esp32_camera.cc` 使用 `Capture()` + `GetLastFrame()`，返回的 `frame_.data` 在下次 `Capture()` 时会被释放并重新分配，**无 fb 池**，无法零拷贝传递指针。因此引入**帧缓冲池**，由相机任务拷贝到池中 buffer 后入队，显示端用完后归还到池。

## 架构示意

```
相机任务: Capture(互斥) -> GetLastFrame -> 拷贝到池 buffer -> xQueueSend(xQueueFrameO)
    |
    v
AI 任务:  xQueueReceive(xQueueFrameO) -> ProcessOneFrame(检测+画框) -> xQueueSend(xQueueAIFrameO)
    |
    v
显示任务: xQueueReceive(xQueueAIFrameO) -> 拷贝后 SetPreviewImage -> ReturnBuffer 到池
```

---

## 人脸检测数据流与 AI 模块职责（结合代码）

整体链路可以概括为：**拍照 → 下采样（“压缩”）→ 原始队列 → AI 检测（格式转换、检测、画框、可选注册）→ AI 队列 → 显示**。下面按步骤对应到代码。

### 1. 拍照 + 下采样 + 入原始队列（FaceCameraTask，`app_ai.cpp`）

| 步骤 | 代码位置 | 说明 |
|------|----------|------|
| 拍照 | `ctx->camera->Capture()` | 持相机互斥，取一帧（640×480，YUYV 或 RGB565）。 |
| 取帧 | `ctx->camera->GetLastFrame(&frame)` | 得到 `frame.data`、`frame.width/height`、`frame.format`。 |
| 取池 buffer | `ctx->pool.TakeBuffer()` | 从帧池取一块 **320×240×2** 的 PSRAM buffer。 |
| “压缩” | `frame.width >= 640 && frame.height >= 480` 时的双重循环 | **2×2 下采样**：从 640×480 每 (2*i, 2*j) 取 2 字节写入 `buf`，得到 320×240×2（非 JPEG 等编码压缩，是分辨率缩小）。 |
| 入队 | `xQueueSend(ctx->q_raw, &qframe, ...)` | `qframe` 指向池 buffer，`width=320, height=240, len=153600, format=相机 format`。 |

未满足 640×480 时走 `memcpy` 分支，只做拷贝、不保证 320×240。

### 2. AI 检测（FaceAITask → ProcessOneFrame，`face_recognition.cpp`）

**AI 模块是在做真实检测，不是原封不动透传。** 每帧会做：

| 步骤 | 代码位置 | 说明 |
|------|----------|------|
| 收帧 | `xQueueReceive(ctx->q_raw, &qframe)` | 从原始队列取一帧（同一块池 buffer）。 |
| 格式统一 | `qframe->format == 3` 分支 | 若为 **YUYV**，用 `esp_imgfx_color_convert` 转成 **RGB565** 并写回 `qframe->data`，`format` 改为 1。 |
| 尺寸统一（若需要） | `!already_320x240` 分支 | 若非 320×240，分配 dst_buf，`dl::image::resize` 缩放到 320×240；当前配置入队已是 320×240，通常**不再做 resize**。 |
| 亮度过滤 | `mean_lum < FACE_DETECT_MIN_LUMINANCE` | 整帧平均亮度过低则**本帧不跑检测**，避免误检。 |
| **人脸检测** | `detect->run(run_img)` | **ESP-DL HumanFaceDetect** 对 320×240 RGB565 做推理，得到 `results`（框 + 置信度）。 |
| 框过滤与坐标映射 | `for (const auto& r : results)`、`sx/sy` | 按 `FACE_DETECT_SCORE_THRESHOLD`、`FACE_DETECT_MIN_BOX_SIZE` 过滤，把 320×240 下的框用 `sx/sy` 映射回当前帧宽高，填入 `last_detection_boxes_`。 |
| **画框** | `DrawFaceBoxesOnRgb565(qframe->data, w, h, last_detection_boxes_)` | 在 **同一块 `qframe->data`** 上画矩形框和 ID/姓名（`face_draw.cpp`），**原地修改**，不是新开一块再传。 |
| 本地/远程“姓名” | `SimulateLocalDetection`、`UpdateLocalDatabase`、可选 `Explain` | 当前检测到人脸时 `person_name = "unknown"`；若开启 `ENABLE_REMOTE_RECOGNITION` 且满足条件会走 **Explain** 或 **SimulateRemoteRecognition**（虚拟姓名），并 **UpdateLocalDatabase** 做“见过/新注册”记录。 |
| 出队 | `xQueueSend(ctx->q_ai, &qframe, ...)` | 把**已画框的同一 qframe**（同一 buffer）送入 AI 队列。 |

因此：**每帧都会跑 HumanFaceDetect，有框则在原图上画框，再带着框送到显示；没有框也会送图，只是框为空。** 不是“只透传不检测”。

### 3. 显示（FaceDisplayTask，`app_ai.cpp`）

| 步骤 | 代码位置 | 说明 |
|------|----------|------|
| 收帧 | `xQueueReceive(ctx->q_ai, &qframe)` | 从 AI 队列取**已画框**的一帧（或 passthrough 下为原始帧）。 |
| 拷贝/转换到 PSRAM 并显示 | `ShowQueuedFrameOnDisplay` | **format=1（RGB565）**：分配一块 PSRAM 的 RGB565 buffer，`memcpy(qframe->data, ...)`。**format=3（YUYV）**：passthrough 模式下队列里是 YUYV，此处分配一块 PSRAM 的 RGB565 buffer，用 `esp_imgfx_color_convert` 做 YUYV→RGB565，再构造 `LvglAllocatedImage` 显示，以便在「不做人脸检测」时仍能刷新画面、验证内存。 |
| 还池 | `ctx->pool.ReturnBuffer(qframe.data)` | 把池 buffer 还回，供相机任务复用。 |

### 小结（对应你的理解）

- **“拍照、压缩、队列、AI 检测、队列、显示”** 是对的：  
  - “压缩” = 当前实现的 **2×2 下采样**（640×480 → 320×240×2），不是 JPEG。  
  - 第一个队列 = 原始帧队列 `q_raw`，第二个队列 = AI 处理后队列 `q_ai`。
- **AI 检测模块**：  
  - **有在做检测**：每帧调用 `HumanFaceDetect::run`，得到人脸框。  
  - **有在原图上画框**：`DrawFaceBoxesOnRgb565` 在 `qframe->data` 上原地画框和文字。  
  - **有做格式/尺寸统一**：YUYV→RGB565，非 320×240 时 resize（当前入队已是 320×240 可省略）。  
  - **有做本地/远程逻辑**：未知脸标 "unknown"，可选 Explain/虚拟姓名与 `UpdateLocalDatabase`。  
  所以**不是**“仅原封不动传输”，而是**检测 + 画框 + 可选注册后再把同一块 buffer 送入 AI 队列给显示**。

### 4. FACE_AI_PASSTHROUGH 模式（`config.h`：`FACE_AI_PASSTHROUGH=1`）

用于**验证内存是否因人脸检测/转换不足**：AI 任务不做任何处理（不调用 `ProcessOneFrame`），直接把 `q_raw` 的帧传到 `q_ai`。此时队列里是 **YUYV（format=3）**，显示任务 `ShowQueuedFrameOnDisplay` 会识别 format=3，在**显示侧**做一次 YUYV→RGB565（PSRAM 分配一块 320×240×2 的 RGB565 buffer），再刷新屏幕。这样既能看到画面刷新，又不会在 AI 侧分配人脸模型、dst_buf、YUYV 转换缓冲，便于对比「开检测」与「仅透传+显示转换」的内存差异。

## 已实现内容

1. **帧结构与池**：`face/face_frame_queue.h`、`face_frame_queue.cc` 中 `QueuedFrame` 与 `FaceFramePool`（TakeBuffer/ReturnBuffer，互斥锁管理）。
2. **配置**：`config.h` 中 `FACE_QUEUE_FRAME_POOL_SIZE`、`FACE_QUEUE_RAW_DEPTH`、`FACE_QUEUE_AI_DEPTH`、`FACE_QUEUE_FRAME_MAX_BYTES` 及任务栈/优先级。
3. **两个 FreeRTOS 队列**：`xQueueFrameO`（原始帧）、`xQueueAIFrameO`（AI 处理后帧），元素为 `QueuedFrame`（值拷贝，内含 `data` 指针）。
4. **相机任务**：`FaceCameraTask`，取池 buffer、`Capture(false)`（持互斥）、`GetLastFrame`、拷贝、入队原始帧。
5. **AI 任务**：`FaceAITask`，从原始帧队列取帧、调用 `FaceRecognition::ProcessOneFrame`（YUYV→RGB565、检测、画框、可选注册）、入队处理后帧。
6. **显示任务**：`FaceDisplayTask`，从 AI 队列取帧、方案 1：拷贝到 PSRAM 后 `SetPreviewImage`、立即 `ReturnBuffer`。
7. **任务**：仅创建 `FaceCameraTask`、`FaceAITask`、`FaceDisplayTask`（无单任务模式）。
8. **MCP 与相机互斥**：`CameraCaptureLockWrapper` 包装 `Capture`/`GetLastFrame`，`GetCamera()` 返回该包装；相机任务在 `Capture` 前后持同一互斥锁。

## 关键文件

| 项目       | 文件/位置 |
| ---------- | --------- |
| 帧结构与池 | `face/face_frame_queue.h`、`face_frame_queue.cc` |
| 队列与任务 | `deep_thumble.cc`（InitializeFaceRecognition、FaceCameraTask、FaceAITask、FaceDisplayTask、ShowQueuedFrameOnDisplay） |
| 检测+画框 | `face/face_recognition.cc`（ProcessOneFrame） |
| 配置       | `config.h`（FACE_QUEUE_*） |

---

## PSRAM 与内部 RAM 的区别；人脸模型存放位置

### PSRAM 与内部 RAM

| 项目 | 内部 RAM（SRAM） | PSRAM（SPIRAM） |
|------|------------------|-----------------|
| **位置** | 芯片内置（ESP32-S3 约 512KB） | 外挂芯片（本板 8MB） |
| **速度** | 快 | 较慢 |
| **默认 malloc/new** | **使用**：C/C++ 默认堆在内部 | 不使用；需显式 `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` |
| **典型用途** | 任务栈、DMA、小缓冲、未指定时的堆 | 大图缓冲、帧池、LVGL 缓存、显式申请的大块 |
| **耗尽后果** | 内部见底时，任何默认堆分配（如 `std::vector`、`malloc`）失败 → 易 abort | 总堆/PSRAM 见底时，大块 PSRAM 分配失败（如预览 600KB） |

所以：**未显式指定 PSRAM 的分配都在内部 RAM**。唤醒词里 `StoreWakeWordData` 用 `std::vector<int16_t>`（默认堆）→ 占内部；帧池用 `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` → 占 PSRAM。

### 人脸识别模型存放在哪

- **HumanFaceDetect（ESP-DL）**：模型权重一般在 **Flash**（只读），或编译进只读段；运行时由组件加载。
- **运行时占用**：推理时的中间缓冲、输入/输出张量由 **ESP-DL 组件** 分配，可能用内部或 PSRAM（取决于组件配置）；日志里 “dl::Model: Minimize()” 表示模型在推理前做最小化，权重/结构仍在进程内，**运行时缓冲**是主要 RAM 占用。
- 若希望模型相关缓冲进 PSRAM，需在 **ESP-DL 或 esp-dl 组件** 中配置（若支持）；当前 deep-thumble 未单独改该组件。

---

## 加入摄像头 + AI 后的内存占用分析

未加 AI 时内存正常，加入摄像头和人脸 AI 后内存吃紧，主要新增/放大的占用如下（按影响从大到小）。

### PSRAM 占用（8MB 总量，大块）

| 模块 | 用途 | 单次/常驻大小 | 何时分配 | 说明 |
|------|------|----------------|----------|------|
| **人脸帧池（FaceFramePool）** | 双队列帧缓冲 | **2 × 153 600 ≈ 300 KB** 常驻 | Init 时 | 队列帧为 **320×240×2**（相机 640×480 做 2×2 下采样入队），`FACE_QUEUE_FRAME_MAX_BYTES=320*240*2`。原 640×480 时约 1.2MB，改后显著降低。 |
| **LVGL 图片缓存** | 界面/图标缓存 | **1 MB** 常驻 | 显示初始化 | `lcd_display.cc`，8MB PSRAM 时 1MB。 |
| **摄像头帧副本（Esp32Camera）** | 每帧 YUYV 数据 | **614 400 B ≈ 600 KB** | 每次 DQBUF，下一帧前释放 | `frame_.data`，640×480×2。与帧池不同：相机内部暂存，按帧分配/释放。 |
| **人脸显示/预览缓冲** | 显示一帧到屏幕 | **614 400 B ≈ 600 KB** | FaceDisplayTask 每帧 1 次；或 Esp32Camera ShowPreviewImage | `app_ai.cpp` 里 `heap_caps_malloc(size_rgb565, PSRAM)` 或 esp32_camera 预览。**同一时刻可能有多处各 600KB。** |
| **人脸 YUYV→RGB565 转换** | 每周期格式转换 | **614 400 B ≈ 600 KB** | ProcessOneFrame 内 format=3 时，周期末释放 | `face_recognition.cpp` 里 `out_buf`。与预览不同时存在，但会与帧池、预览叠加。 |
| **人脸检测输入（dst_buf）** | 缩放到 320×240 做检测 | **153 600 B ≈ 150 KB** | 每周期检测时，用后即释 | 已改为 PSRAM 优先。 |
| **唤醒词编码任务栈** | AFE 编码用栈 | **24 KB** 常驻 | AfeWakeWord 初始化 | `heap_caps_malloc(4096*6, MALLOC_CAP_SPIRAM)`。 |

**PSRAM 小结**：常驻约 **1MB（LVGL）+ 300KB（帧池 320×240×2）+ 24KB**；按帧/按周期还有多块 **600KB 级** 的分配（相机帧、预览、YUYV 转换）在 common 路径。队列改为 320×240×2 后，帧池从约 1.2MB 降到约 300KB，且人脸检测在 320×240 入队时**无需再分配 150KB 的 dst_buf 做缩放**，进一步省 PSRAM/内部 RAM。

### 内部 RAM 占用（约 300KB 级可用，紧张）

| 模块 | 用途 | 单次/常驻大小 | 何时分配 | 说明 |
|------|------|----------------|----------|------|
| **人脸帧池元数据** | 指针数组、空闲表、互斥 | 少量（几十 B × pool_size） | Init | `frame_queue.cpp`：`buffers_`、`free_list_` 为 `MALLOC_CAP_INTERNAL`。 |
| **三个 AI 相关任务栈** | FaceCamera / FaceAI / FaceDisplay | **4+4+4 = 12 KB** | app_ai Start | `config.h`：各 4096。**加入 AI 后新增。** |
| **唤醒词 PCM 缓冲（AfeWakeWord）** | 存约 2 秒唤醒音频 | **每块 512×2 B，多块累计约数十 KB** | 每次 fetch 后 StoreWakeWordData | `afe_wake_word.cc:151` 用 `std::vector<int16_t>` **默认堆（内部 RAM）**。内部 RAM 见底时这里 `operator new` 失败 → abort。**加入 AI 后未变，但与其他占用叠加后易触发崩溃。** |
| **AFE / WakeNet 内部** | 模型、状态、任务栈 | 依赖 esp-afe-sr，量级较大 | 音频初始化 | 常见为内部 RAM + 部分 PSRAM，具体看组件配置。 |
| **ESP-DL 人脸检测模型** | HumanFaceDetect | 模型权重 + 运行时缓冲 | FaceRecognition 首次 ProcessOneFrame | `new HumanFaceDetect()`，通常有内部或 PSRAM 占用。 |
| **JPEG 编码（Explain/拍照）** | 软编码中间缓冲 | `jpeg_calloc_align` 多为内部 | 调用编码时 | 内部紧张时易 “alloc/convert input failed”。 |

**内部 RAM 小结**：加入摄像头和 AI 后，**任务栈 + 人脸模型 + AFE + 唤醒词 PCM 缓冲** 等都在内部堆上分配；日志里 `free sram: 58151` 是**内部+PSRAM 合并**，内部实际更少，再在 `StoreWakeWordData` 里用默认堆分配 vector 就容易失败并 abort。

### 占用从大到小（便于优先优化）

1. **人脸帧池**：2 × 614400 ≈ **1.2 MB PSRAM** 常驻。  
2. **LVGL 缓存**：**1 MB PSRAM** 常驻。  
3. **多块 600KB 级 PSRAM**：相机帧、预览、YUYV 转换等，按帧/周期叠加，**单帧/单周期可再占 1.2～1.8 MB**。  
4. **唤醒词 PCM**：内部 RAM，多块小 vector 累计 **数十 KB**，但在内部见底时成为**直接崩溃点**。  
5. **三个 AI 任务栈**：**12 KB 内部 RAM**。  
6. **人脸检测 dst_buf**：已改为 PSRAM 优先，**150 KB** 不再占内部。  
7. **ESP-DL 模型 + AFE**：内部/PSRAM 均有，量级大但较难在板级裁剪。

若要继续在 **boards/deep-thumble 内** 省内存，可优先：再减帧池（如 pool=1）、减队列深度、或降分辨率（`FACE_QUEUE_FRAME_MAX_BYTES`）；内部 RAM 的根治仍需在 `afe_wake_word.cc` 中把 `StoreWakeWordData` 改为使用 PSRAM（见下文）。

---

## 内存降低措施（缓解内部 RAM / PSRAM 耗尽）

根因是内部 RAM 与 PSRAM 被人脸/相机/显示/唤醒词等模块占满。以下改动在代码中已落实，用于有效降低内存占用：

| 措施 | 位置 | 效果 |
|------|------|------|
| **队列帧 320×240×2** | `config.h`、`app_ai.cpp`、`face_recognition.cpp` | 相机 640×480 入队前 2×2 下采样，池每 buffer **153 600 B**（原 614 400），2 个池约 **300KB**（原 ~1.2MB）；检测输入已是 320×240 时**不再分配 dst_buf、不做 resize**，省 ~150KB 与 CPU。 |
| **帧池与队列缩小** | `config.h` | `POOL_SIZE` 2、`RAW_DEPTH`/`AI_DEPTH` 2，队列积压更少。 |
| **人脸检测输入缓冲用 PSRAM 优先** | `app_ai/src/face_recognition.cpp` | `dst_buf`（320×240×2≈150KB）先试 `MALLOC_CAP_SPIRAM`，再回退内部：减少对 **内部 RAM** 的占用。 |
| **who rgb888 缓冲用 PSRAM** | `ai/who_ai_utils.cpp` | `app_camera_decode` 中 rgb888 缓冲改为 `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`，避免 640×480×3≈**900KB 内部 RAM**。 |
| **LVGL 图片缓存缩小** | `main/display/lcd_display.cc` | 8MB PSRAM 时由 2MB→**1MB**，约省 **1MB PSRAM**。 |
| **人脸 AI 任务栈缩小** | `config.h` | `FACE_AI_TASK_STACK` 6144→4096，约省 **~2KB 内部 RAM**。 |

**队列帧 320×240×2**：双队列已改为 `FACE_QUEUE_FRAME_WIDTH/HEIGHT=320/240`，相机 640×480 在入队时做 2×2 下采样，池子与队列内均为 320×240×2（约 150KB/帧）。人脸检测输入本就是 320×240，入队已是 320×240 时**不再分配 dst_buf、不再做 resize**，既省 PSRAM 又省内部 RAM。显示端收到 320×240 帧，由 LVGL/Display 按需缩放。若仍 OOM，可再关闭部分常驻功能。详见 `docs/psram-distribution-deep-thumble.md`。

**关于唤醒词崩溃（abort 在 AfeWakeWord::StoreWakeWordData）**：崩溃发生在 `main/audio/wake_words/afe_wake_word.cc` 第 151 行，`StoreWakeWordData` 内用 `std::vector<int16_t>(data, data+samples)` 在默认堆（内部 RAM）上分配；内部 RAM 见底时 `operator new` 失败导致 abort。本目录（deep-thumble）内已通过上述措施尽量省内部 RAM，但唤醒词该处分配在 common 代码中，无法在 boards 内单独替换。若在仅改 deep-thumble 的前提下仍反复崩溃，**唯一可靠修法**是：在 `main/audio/wake_words/afe_wake_word.cc` 中将 `StoreWakeWordData` 改为使用 `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` 存 PCM，并在 `pop_front`/`clear`/析构时对指针做 `heap_caps_free`（即仅改该文件一次，不新增 deep-thumble 外常驻改动）。

---

## 最新崩溃：WakeNet 初始化 PSRAM 分配失败 → StoreProhibited（已用延迟启动人脸管道缓解）

### 现象（来自最新 log）

- 激活完成、状态切到 idle 后，出现：`Item psram alloc failed. Size: 32788 = 32768 x 1 + 16 + 4`，随后 `Guru Meditation Error: Core 0 panic'ed (StoreProhibited)`，PC 在 `memcpy`，`A2 = 0x00000000`（NULL）。
- Backtrace：`memcpy` ← `read_model_params_copy` (hufzip_model.c) ← `read_model_params` ← `model_create` (wakenet9_quantized.c) ← `model_init` ← `afe_init_wn` ← `afe_create_from_config` ← `AfeWakeWord::Initialize` ← `AudioService::EnableWakeWordDetection` ← `Application::HandleStateChangedEvent`。

### 根因

1. **状态切到 idle** 时，Application 调用 `EnableWakeWordDetection(true)`，进而执行 **AfeWakeWord::Initialize**。
2. AFE/WakeNet（esp_sr_lib）在初始化时需要约 **32KB PSRAM**（32788 字节），调用 `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` 时失败（**Item psram alloc failed**）。
3. 库内未对分配失败做检查，将 **NULL** 传给 `memcpy`，导致向地址 0 写 → **StoreProhibited** 并重启。

此时 `free sram: 54259`（内部+PSRAM 合并），人脸管道已在启动时运行（帧池、双队列、检测任务等已占用 PSRAM），到 idle 时 PSRAM 或总堆余量不足，无法再拿出 32KB 连续块。

### 缓解措施（已实现）：延迟启动人脸管道

- **思路**：让人脸管道**晚一点启动**，这样在状态切到 idle（约 11s）时，**AFE 先初始化**，此时尚未创建人脸帧池和双队列任务，PSRAM 余量更大，32KB 分配更容易成功；之后再启动人脸管道。
- **实现**（仅改 deep-thumble）：
  - `config.h`：增加 `FACE_RECOGNITION_DELAYED_START_MS`（默认 15000 ms）。
  - `deep_thumble.cc`：构造函数中不再直接调用 `InitializeFaceRecognition()`，改为创建任务 `DelayedFaceInitTask`，在 `vTaskDelay(FACE_RECOGNITION_DELAYED_START_MS)` 后调用 `InitializeFaceRecognition()`。
- **效果**：约 0～15s 内无人脸预览；约 11s 时状态到 idle、AFE 初始化，此时无帧池占用，32KB PSRAM 分配成功率提高；约 15s 后人脸管道启动，预览与检测正常。若仍出现「Item psram alloc failed」，可适当增大 `FACE_RECOGNITION_DELAYED_START_MS` 或继续减少其它 PSRAM 占用。

---

## esp-who 对齐的人脸检测实施（本阶段：检测正确 + LCD 显示人脸框）

### 实施概要

- **统一检测核心**：`app_ai/include/face_detect_core.hpp`、`app_ai/src/face_detect_core.cpp`。`RunFaceDetectCore(QueuedFrame*, std::vector<FaceDetectResult>*)` 完成 YUYV→RGB565、resize 320×240、亮度过滤、`HumanFaceDetect::run`、与 esp-who 一致的 rescale（inv_rescale + 边界 clamp）及阈值/最小框过滤，输出已映射到帧尺寸的 `FaceDetectResult`。
- **app_face_detection**：`RunFaceDetectionAndLog` 仅调用 `RunFaceDetectCore`，根据返回结果打 log（人脸数、框、score）。
- **face_recognition**：`ProcessOneFrame` 调用 `RunFaceDetectCore`，将结果转为 `FaceBox` 填入 `last_detection_boxes_`，再 `DrawFaceBoxesOnRgb565` 画框；人脸识别（数据库、姓名）预留接口，下一阶段实现。检测器改由检测核心内部持有，`FaceRecognition` 不再持有 `esp_dl_detect_handle_`。
- **显示**：`task_face_ai` 在 `FACE_AI_PASSTHROUGH=0` 时对每帧执行检测并画框，同一 buffer 送入 `q_ai`；`task_face_display` 从 `q_ai` 取帧并 `ShowQueuedFrameOnDisplay`，LCD 显示带框画面。本阶段未改 `task_face_camera` / `task_face_display` 职责。
- **配置**：`config.h` 中 `FACE_AI_PASSTHROUGH=0` 启用检测与画框；阈值/最小框/亮度仍为 `FACE_DETECT_*`。

### 整条链路与 face_recognition 职责

- **拍照**：`task_face_camera` 调用 `Capture()`、`GetLastFrame()`，下采样到 320×240 入池 buffer，`xQueueSend(ctx->q_raw, &qframe)`。
- **检测 + 画框**：`task_face_ai` 从 `q_raw` 取帧 → `RunFaceDetectionAndLog`（打 log）→ `ProcessOneFrame`。`ProcessOneFrame` 内：`RunFaceDetectCore` 做检测 → 结果转 `FaceBox` → `DrawFaceBoxesOnRgb565(qframe->data)` 画框；识别暂未实现（person_name 为 "unknown"），预留 UpdateLocalDatabase/Explain。**送入 q_ai 由 task_face_ai 在 ProcessOneFrame 返回后执行**（`xQueueSend(ctx->q_ai, &qframe)`），face_recognition 不负责入队。
- **显示**：`task_face_display`（app_ai 当前启动的为 FaceDisplayTask）从 `q_ai` 取帧 → `ShowQueuedFrameOnDisplay` → 归还池 buffer。解释任务（FaceExplainTask）本阶段已关闭，仅启动显示任务以确认检测结果正常。

### 与 components 的关系

- **检测模型**：与 esp-who 一致，使用 `HumanFaceDetect`（human_face_detect 组件）；`face_detect_core` 内 rescale 逻辑与 who_detect 对齐。
- **画框**：使用 app_ai 自实现 `DrawFaceBoxesOnRgb565`（face_draw）；components 中 who_detect_result_handle 的 `draw_detect_results_on_img` 基于 `dl::image::img_t` 与 `result_t`，接口与当前 RGB565 buffer + FaceBox 不同，暂不替换。
- **who_detect / who_recognition**：依赖 WhoFrameCapNode（从 who_cam 取帧），当前帧来自 QueuedFrame + 双队列，无法直接复用 WhoDetect 类；保持“逻辑对齐、不接 WhoDetect 管线”的方案。

---

## 画框逻辑说明：当前实现 vs esp-who，以及图像格式一致性

### 1. 当前画框逻辑（face_draw）

- **接口**：`DrawFaceBoxesOnRgb565(uint8_t* rgb565_buf, uint16_t buf_w, uint16_t buf_h, const std::vector<FaceBox>& boxes)`。
- **数据**：原始 RGB565 缓冲指针 + 宽高 + 自有的 `FaceBox` 列表（`x, y, width, height, id, name`）。
- **行为**：在缓冲上直接画矩形框（厚度 5）、ID 数字、姓名字符（5×7 点阵）；不依赖 `dl::image::img_t` 或 `dl::detect::result_t`。

### 2. esp-who / components 画框逻辑（who_detect_result_handle）

- **接口**：`who::detect::draw_detect_results_on_img(const dl::image::img_t &img, const std::list<dl::detect::result_t> &detect_res, const std::vector<std::vector<uint8_t>> &palette)`。
- **数据**：
  - `img_t`：`data + width + height + pix_type`（支持 RGB565 / RGB888 等），即“图像描述”而非裸指针。
  - `result_t`：`box[4]`（x1,y1,x2,y2）、`score`、`category`（用于 palette 下标）、可选 `keypoint`（5 点）。
  - `palette`：按类别给的 RGB 颜色，如 `{{255,0,0}}` 表示第 0 类为红。
- **行为**：内部调用 esp-dl 的 `dl::image::draw_hollow_rectangle(img, box[0..3], palette[res.category], 2)` 和 `draw_point` 画关键点；**画框本身来自 esp-dl**（`managed_components/espressif__esp-dl/vision/image/dl_image_draw.hpp`），who_detect_result_handle 只是按 `result_t` 列表和 palette 转调。

### 3. 为何“暂不替换”

- **接口不一致**：我们上游是 `QueuedFrame`（`data` + `width/height`）+ 检测核心输出的 `std::vector<FaceDetectResult>`（后转成 `FaceBox`）。who 侧是 `img_t` + `std::list<dl::detect::result_t>` + palette；`result_t` 还带 `category`、`keypoint`，我们当前只用了 box + score。
- **依赖链**：若直接调用 `who::detect::draw_detect_results_on_img`，需要链接 who_detect_result_handle，其依赖 who_detect（再依赖 who_frame_cap、who_cam），会拉入整条 who 管线，而我们现在只用 QueuedFrame 双队列，不需要 WhoFrameCapNode。
- **功能**：当前 face_draw 已满足“框 + ID/姓名”的显示需求；esp-who 那套多了一个“按 category 选颜色 + 关键点”，我们人脸只有一类，可后续再对齐。

### 4. 送入 AI 检测的图像帧是否与 esp-who 一致

**一致。**

- **格式**：我们都是 **RGB565**。esp-who 示例里 WhoS3Cam 使用 `PIXFORMAT_RGB565`，`cam_fb_t` 转成 `dl::image::img_t` 时 `pix_type = DL_IMAGE_PIX_TYPE_RGB565`。我们 `RunFaceDetectCore` 里对 `QueuedFrame` 若为 YUYV 会先转成 RGB565，再构造 `dl::image::img_t`（`pix_type = DL_IMAGE_PIX_TYPE_RGB565`）交给 `HumanFaceDetect::run(img)`，与 esp-who 一致。
- **分辨率**：检测输入均为 **320×240**。esp-who 通过 `get_cam_frame_size_from_lcd_resolution()` 取帧尺寸，我们相机下采样后入队为 320×240（`FACE_QUEUE_FRAME_WIDTH/HEIGHT`），非 320×240 时在检测核心内 resize 到 320×240 再 `run()`，与 who 侧对模型的输入要求一致。
- **字节序**：若存在 BIG_ENDIAN/LE 差异，我们通过 `config.h` 的 `FACE_DETECT_RGB565_BYTE_SWAP` 在检测前做像素字节对调，与文档 `docs/face-detection-root-cause.md` 一致。

因此：**送入 AI 检测的一帧（320×240 RGB565，必要时字节对调）与 esp-who 的检测输入格式保持一致**；差异只在“谁产帧”（WhoS3Cam + WhoFrameCapNode vs 我们相机 + QueuedFrame）和“谁画框”（who_detect_result_handle vs face_draw）。

### 5. 若要改为 esp-who 风格画框，可怎么做

两种做法（由简到繁）：

**方案 A：仅复用 esp-dl 的绘制 API（推荐，不引入 who 组件）**

- 我们已有 esp-dl（human_face_detect 依赖），其中 `dl::image::draw_hollow_rectangle(img, x1, y1, x2, y2, color_rgb_vector, line_width)` 支持在 `img_t`（RGB565）上画框，颜色传 `std::vector<uint8_t>{R,G,B}`，内部会转成 RGB565 再画。
- **修改方式**：在 face_recognition（或单独一层）中，用当前帧构造 `dl::image::img_t`（`data = qframe->data`，`width/height`，`pix_type = DL_IMAGE_PIX_TYPE_RGB565`），对每个 `FaceDetectResult` 调用 `dl::image::draw_hollow_rectangle(img, (int)box[0], (int)box[1], (int)box[2], (int)box[3], {255,0,0}, 2)`；若需要关键点再调 `dl::image::draw_point`。这样“画框逻辑”与 esp-who 使用的 esp-dl 接口一致，且不依赖 who_detect_result_handle。
- **头文件**：`#include "dl_image_draw.hpp"`、`#include "dl_image_define.hpp"`（或 `dl_image.hpp`），由 esp-dl 提供。

**方案 B：直接调用 who_detect_result_handle::draw_detect_results_on_img**

- 需要构造 `std::list<dl::detect::result_t>`：从当前 `FaceDetectResult` 或从检测核心的“原始 result_t 列表”转出（若保留 category/keypoint 可一并填入）；再给一个 `palette`，如 `{{255,0,0}}`。
- 将 `qframe->data` 包装成 `dl::image::img_t`，然后调用 `who::detect::draw_detect_results_on_img(img, list_result, palette)`。
- **代价**：需在板级或 main 里链接 who_detect_result_handle；该组件依赖 who_detect，会拖入 who_frame_cap、who_cam 等，需评估 CMake/依赖是否接受。若只想“画框风格一致”，方案 A 足够。

**小结**：当前画框用“自实现 RGB565 + FaceBox”是接口与依赖上的折中；**检测输入与 esp-who 一致（320×240 RGB565）**。若要和 esp-who 画框一致，优先用 **方案 A（仅用 esp-dl 的 draw_hollow_rectangle/draw_point）**，无需引入 who 组件。
