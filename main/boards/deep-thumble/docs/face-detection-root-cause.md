# 人脸检测根因排查

## 0. human_face_detect 组件支持的输入格式

**确定支持**：

- **RGB565**：组件内部 ImagePreprocessor 使用 `DL_IMAGE_CAP_RGB_SWAP | DL_IMAGE_CAP_RGB565_BIG_ENDIAN`（ESP32-S3），即按 **RGB565** 输入、**BIG_ENDIAN** 字节序解析。
- **RGB888**：esp-dl 的 `ImagePreprocessor::preprocess` 只要求 `get_img_channel(img)==3`，`resize`/`resize_loop` 对 `src_img.pix_type` 有 **RGB888** 和 **RGB565** 分支，故 **RGB888** 也可作为输入。

**不支持**：

- **GRAY（灰度）**：`get_img_channel(GRAY)==1`，而模型 mean 为 3 通道，`preprocess` 里 `assert(get_img_channel(img)==m_mean.size())` 会失败。

**结论**：组件**支持 RGB565 和 RGB888**；当前工程用 RGB565 是合规的。若怀疑 RGB565 字节序问题，可改为先转成 **RGB888** 再构造 `img_t`（`pix_type=DL_IMAGE_PIX_TYPE_RGB888`）传入 `run(img)`，以规避 BIG_ENDIAN/LE 差异。

---

## 1. 关于 log 里的两条 Minimize() 警告

```
W (16966) dl::Model: Minimize() will delete variables not used in model inference, which will make it impossible to test or debug the model.
W (17006) dl::Model: Minimize() will delete variables not used in model inference, ...
```

**含义**：ESP-DL 在加载 MSR、MNP 两个模型时，会调用 `m_model->minimize()`，把推理用不到的中间变量从内存里清掉，以节省 RAM。  
**结论**：这是**正常行为**，不是错误；模型仍会正常做推理。只是之后无法用组件的 test/debug 接口做完整测试。  
**出处**：`managed_components/espressif__esp-dl/dl/model/src/dl_model_base.cpp` 中 `Model::minimize()`。

---

## 2. 框坐标顺序：使用 FACE_DETECT_BOX_SWAP_XY=0

**ESP-DL 定义**：`dl_detect_define.hpp` 中 `result_t.box` 为 **[left_up_x, left_up_y, right_down_x, right_down_y]**；MSR/MNP 后处理源码按 [x0,y0,x1,y1] 写入。交换 XY 只会把「框在左侧」变成「框在上侧」，不解决检测不可靠的根因。

**根因**：检测结果不可靠、框集中在一侧，主要来自 **RGB565 字节序不一致**（见下节）。应保持 **FACE_DETECT_BOX_SWAP_XY=0**。

---

## 3. 输入图像格式与 RGB565 字节序（检测不可靠的主因）

**组件要求**：

- **human_face_detect**（ESP32-S3）的 ImagePreprocessor 使用 **`DL_IMAGE_CAP_RGB565_BIG_ENDIAN`**（见 `human_face_detect.cpp`），即按 **大端 16 位** 解析每个像素：高字节为 R/高 G，低字节为 B/低 G（见 esp-dl `dl_image_define.hpp` 的 `DL_IMAGE_BIG_ENDIAN_RGB565_BIT1/2/3`）。
- 相机与 **esp_imgfx_color_convert** 输出均为 **RGB565 小端（LE）**：内存中低字节在前。若不转换，组件会把 LE 数据当 BE 读，**R/G/B 拆错**，模型看到错误颜色 → 误检、漏检、框集中在一侧等。

**正确用法**：

- **FACE_DETECT_RGB565_BYTE_SWAP=1**：在调用 `run(img)` 前，对送入检测的 buffer 做**每像素高/低字节对调**（或传入对调后的副本），使组件按 BIG_ENDIAN 读到的数值与真实 RGB565 一致。当前实现：拷贝到副本后对副本做 `(*p >> 8) | (*p << 8)`，再以副本构造 `img_t` 传入 `run()`。
- 模型加载：`HumanFaceDetect()` 无参即用默认 MSRMNP_S8_V1；模型通过 component CMake 的 `target_add_aligned_binary_data` 嵌入（CONFIG_HUMAN_FACE_DETECT_MODEL_IN_FLASH_RODATA），无需应用层再传路径。

---

## 4. 排查检查清单

| 项 | 说明 | 状态 |
|----|------|------|
| Minimize() 警告 | 正常，非错误 | 可忽略 |
| box 顺序 | ESP-DL 为 [x0,y0,x1,y1]，应用 SWAP_XY=0 | 建议改为 0 |
| 输入尺寸 | 320×240 RGB565，与组件内部 resize 一致 | 已满足 |
| YUYV→RGB565 | esp_imgfx_color_convert，BT601 | 已做 |
| RGB565 字节序 | 组件 BIG_ENDIAN vs 我们 LE，可能不一致 | 待验证（可试字节对调） |
| 相机下采样 | 2×2 取点，每像素 2 字节，format 透传 | 已做 |

---

## 5. 建议修改（config.h）与可选试验

- **FACE_DETECT_BOX_SWAP_XY**：设为 **0**（与 ESP-DL result_t 一致）。已默认改为 0。
- **FACE_DETECT_RGB565_BYTE_SWAP**：默认 0。若 SWAP_XY=0 后仍异常（误检/漏检/框错位），可试设为 **1**，在检测前对 RGB565 每像素做高/低字节对调，用于排查组件 BIG_ENDIAN 与相机 LE 不一致。  
若改为 1 后效果明显改善，可基本确认是 RGB565 字节序问题。
- **FACE_DETECT_USE_RGB888**：若 byte_swap=1 仍多框误检，可试 **1**：检测前将当前帧从 RGB565 转为 RGB888（按 LE 正确拆 R/G/B），再以 `DL_IMAGE_PIX_TYPE_RGB888` 传入 `run(img)`，彻底绕开 RGB565 字节序；组件 esp-dl 的 ImagePreprocessor 支持 3 通道输入（`get_img_channel(img)==3`），resize 会按 pix_type 分支处理。

---

## 6. 关于「被其他进程打断」是否影响检测精度

**结论：不会。**

- **帧缓冲**：face_ai 从 q_raw 取到的 `qframe` 独占一块 pool buffer，在本次 `RunFaceDetectCore` 期间**不会被** face_camera 或显示任务写入；face_camera 使用池里另一块 buffer。
- **抢占**：face_ai 被抢占只会拉长本次 `run()` 时间，**不会**改写当前帧内容；若没有 DMA/多线程写同一块内存，不会因「被打断」而得到错误像素。
- **多框、x 常为 0**：更可能是**输入图像内容或格式**（如 RGB565 解析、字节序仍不对，或 DVP 输出与组件假设不一致）导致，而非任务调度打断。

---

## 7. 全黑图自检仍多框：模型/预处理管线异常（当前现象）

**现象**：自检用 240×240 RGB565 **全 0** 缓冲跑 `run(black_img)`，期望 0 张脸，实际得到 **6 张脸**（或更多）；实拍时全黑/挡镜头也大量框。

**说明**：全黑图与相机字节序无关（全 0 在 LE/BE 下一致），说明问题在**模型加载、预处理或模型权重**，而不是「相机数据格式」。

**可能原因**（按优先级）：

1. **模型输入未正确写入（首帧未初始化）**  
   若自检**跑两次**全黑图：第一次 N 张脸、第二次 0 张脸，则可能是模型/预处理器内部 buffer 首次未清零，首帧读到垃圾。当前自检已改为「跑两次」并打 log `1st=%lu 2nd=%lu`，可据此判断。

2. **预处理与模型输入 layout 不一致**  
   MSR 模型输入为 120×160×3（HWC），ImagePreprocessor 从 model 取 shape、mean={0,0,0}、std={1,1,1}，做 resize + RGB565→RGB888_QINT8。若某处 stride/对齐错误，可能只写部分像素，其余为未初始化。

3. **嵌入的模型二进制异常**  
   虽 RODATA 大小约 191248 字节正常，仍可核对：  
   - 构建后 `build/espdl_models/human_face_detect.espdl` 与 `managed_components/.../models/s3/` 下两文件打包后大小一致；  
   - 或用官方 esp-who 示例（同一板型/同一 CONFIG）跑全黑图，若官方示例全黑为 0 张脸而本工程为多张，则差异在本工程集成方式。

4. **模型在「全常数输入」下行为异常**  
   部分检测器在输入全为同一常数（如 0）时会产生高置信度输出（out-of-distribution）。若自检两次均为多张脸且分数很高，需结合 2、3 排查预处理与模型是否匹配。

**与任务优先级是否有关？**

- **基本无关**。自检流程是：当前任务内 `heap_caps_malloc` → `memset(0)` → `run(black_img)` 两次，中间没有把 buffer 交给其他任务，也没有被 DMA/其他任务写。输入是确定的全 0，两次都得到 6 张脸，说明是**同一份全黑数据被模型/预处理一致地误判**，而不是「偶尔被写坏」或「调度导致数据错」。  
- 若怀疑优先级，可把 face_ai 任务提到最高跑一次自检：若仍是 1st=6 2nd=6，即可排除优先级影响。

---

## 8. 如何进一步确定问题：验证方法清单

在 **1st=6、2nd=6**（全黑图两次都多框）的前提下，可按下面顺序做，缩小范围：

| 步骤 | 做法 | 若结果 → 说明 |
|------|------|----------------|
| **1. 同板跑官方示例** | 在同一块板上编译并运行 esp-who 的 `human_face_recognition` 或 `object_detect`（human_face_detect），用全黑或挡镜头测几帧 | 官方全黑 **0 张脸** → 差异在本工程集成（分辨率/格式/调用方式）；官方也**多张脸** → 可能是板子/驱动/组件在该环境下的共性问题 |
| **2. 自检改用 RGB888 全黑** | 用 240×240×3 全 0 的 RGB888 做自检（与当前 RGB565 全黑自检并列），看 log `black RGB888: 1st=? 2nd=?` | RGB888 全黑也 **6/6** → 问题不在 RGB565 解析，而在**公共路径**（resize、量化、或模型）；若 RGB888 为 **0/0** → 问题在 RGB565→模型输入这条路径 |
| **3. 核对模型二进制** | 构建后对比 `build/espdl_models/human_face_detect.espdl` 大小（约 191KB），或与 esp-who 同 target 同选项构建产物对比 | 大小/内容一致 → 排除嵌入错包；不一致 → 检查 sdkconfig、组件版本、pack 脚本 |
| **4. 试分辨率（320×240 + 120×160）** | 自检已包含 **320×240** 与 **120×160** 全黑（各 RGB565 + RGB888，跑两次）。120×160 为 MSR 模型原生输入，**无需 resize** | 若 **120×160 全黑为 0/0** 而 240×240、320×240 仍多张脸 → 问题在**预处理 resize**；若 120×160 也多张脸 → 问题在模型或量化等非缩放路径。**当前现象**：120×160 全黑反而更多（10 vs 6）→ 基本排除 resize，问题在模型/量化或输入解释。 |
| **5. 读 MSR 模型输入（dump）** | 自检结束后会打 log：`MSR input: shape=[...] exp=? dtype=?` 与 `MSR input first 16 bytes: ...`（run 后读，可能已被推理覆盖） | 若 **前 16 字节全 0** → 预处理很可能写了零，**模型在零输入下误检**（权重/结构或量化解释问题）；若 **非零** → 可能是推理覆盖，或预处理未写零，需在组件内「preprocess 后、run 前」再 dump 一次确认。 |

**建议**：优先做 **步骤 1**（同板跑官方示例），能最快判断是「本工程集成」还是「环境/组件共性」；再做 **步骤 2**（RGB888 全黑自检），区分是否 RGB565 路径特有。

**当前现象小结（根据你提供的 log）**：

- 240×240 / 320×240 全黑：RGB565 与 RGB888 都是 6 张脸 → 问题**不在 RGB565 解析**，在公共路径。
- 120×160 全黑（MSR 原生尺寸、无 resize）：**10 张脸**（比 6 更多）→ 问题**不在 resize**，在**模型或量化/归一化**对「全零输入」的处理。
- 下一步：看新加的 **MSR input shape / exponent / first 16 bytes** log。若前 16 字节为 0，可基本认定是**模型在零输入下误检**（或权重/二进制异常）；若非 0，需在组件内 preprocess 后、run 前再 dump 一次以确认预处理是否写零。

---

## 9. 根据「MSR input」log 的分析（你当前固件）

你提供的 log：

```
MSR input: shape=[1,120,160,3] exp=1 dtype=3
MSR input first 16 bytes: -128 -82 -128 -88 -128 -97 -128 -94 -128 -93 -128 -93 -128 -98 -128 -98
```

**可确定的**：

- **shape=[1,120,160,3]**：与 MSR 期望的 120×160×3（HWC）一致，输入尺寸/布局正常。
- **exp=1, dtype=3**：dtype=3 为 INT8，exponent=1 为量化缩放，符合常规 INT8 模型。

**前 16 字节非零（-128、-82 等）的含义**：

- 这段是在 **run(black_img) 之后** 读的，推理已经执行完，**输入 buffer 很可能已被第一层或中间层写覆盖**，所以当前读到的不一定是「预处理写入的全黑」。
- 若预处理对全黑写的是 0（mean=0, std=1 时 0→0），则这些 -128/-82 等是**推理产生的数据**，不能据此判断预处理有没有写零。
- 若预处理对全黑做了「127.5 中心化」等（0→约 -128），则 -128 可能来自预处理；但组件构造是 mean={0,0,0}, std={1,1,1}，理论上全黑应为 0，故**更可能是推理覆盖**。

**结论与下一步**：

1. **单凭这份 log 无法区分**：「预处理没写零」还是「模型在零输入下误检」——因为读的是 run 之后的内容。
2. 若要确认预处理是否对全黑写 0：需在 **human_face_detect** 组件里、**preprocess() 之后、model->run() 之前** 再打一次「MSR input 前 16 字节」。若那时为全 0 → 可认定是**模型在零输入下误检**（或权重/二进制异常）；若那时就非 0 → 预处理或量化对「黑」的解释有问题。
3. **不改组件时的实用做法**：把全黑多框视为**模型/权重在「零或近零输入」下的已知行为**，在应用层做缓解：提高 score 阈值、或对「整图过暗/方差过小」的帧直接不跑检测或丢弃结果，减少误框。

---

## 10. 与 ref/face_detect_black_test 对比：主工程为 6 张脸、ref 为 0 张脸

**现象**：同一块板、同一 human_face_detect 组件（~0.3.0）、同一 CONFIG（RODATA、MSRMNP_S8_V1），  
- **ref 工程**（`face_detect_black_test`）：240×240 全黑 RGB565 → **1st=0 2nd=0** ✓  
- **主工程**（deep-thumble）：240×240 全黑 RGB565 → **1st=6 2nd=6** ✗  

说明**模型与预处理在「干净环境」下可以对全黑正确返回 0**，问题出在**主工程的环境或集成方式**。

**可能原因（按优先级）**：

1. **创建时机与堆状态**  
   - ref：在 `app_main()` 里**最先**创建 detector、立刻跑全黑，此时几乎无其他 PSRAM/堆占用。  
   - 主工程：detector 在 **FaceAITask 首次跑检测时**才创建，此时 FaceCameraTask、队列、帧池等已占用大量 PSRAM。  
   - 若 human_face_detect / esp-dl 内部有**未清零的 buffer**（或依赖「首次分配得到零ed 内存」），在 ref 里会拿到较「干净」的块（0），在主工程里可能拿到**复用块（残留数据）**，导致预处理或模型读到非零 → 误检。

2. **任务/栈上下文**  
   - ref 在 main 任务、主工程在 FreeRTOS 任务里创建并 run。若组件内部有未初始化的栈上变量或与任务相关的静态状态，不同上下文可能表现不同（主工程更可疑）。

3. **链接/全局状态**  
   - 主工程链接了大量其他模块，若某处有未初始化全局或与 detector 共享的静态 buffer，可能被别的代码写坏，仅在主工程出现。

**建议验证**：

- **在主工程里尽量提前创建 detector 并跑全黑**：在应用层在**启动人脸管道之前**（例如在 `app_main` 末尾或 idle 前）先 `new HumanFaceDetect(..., false)`、跑两次 240×240 全黑、打 log，再继续正常启动 camera/AI 任务。若此时得到 **0/0**，而把创建改回「首次 RunFaceDetectCore 时」又得到 6/6，可基本认定是**创建时机/堆状态**导致（未清零或复用内存）。
- 若提前创建仍 6/6：再对比两边的 **sdkconfig**（PSRAM、优化、LTO 等）和 **组件解析版本**（`managed_components` 下 human_face_detect / esp-dl 的版本是否完全一致）。

**实际运行结果（主工程「提前创建」）**：

- 在 `app_ai::Start()` 里、在创建 FaceCameraTask / FaceAITask **之前** 调用 `CreateFaceDetectorEarly()`，得到：  
  **`early black test (before face tasks): 1st=6 2nd=6`**
- 即：**仅把创建时机提前到「Face 任务之前」仍得到 6 张脸**，未复现 ref 的 0/0。

**可推出的结论**：

1. **当前「提前」仍不够早**：调用 `CreateFaceDetectorEarly()` 时，主工程已经执行过 `pool.Init`（2×115200 字节）、`xQueueCreate`、`new FaceRecognition` 等，堆布局和 ref（app_main 里几乎只做 alloc black + new detector）不同，可能仍拿到复用/非零内存。
2. **或差异不在创建时机**：主工程与 ref 在 **sdkconfig**（PSRAM、优化、LTO）、**组件解析版本**（human_face_detect / esp-dl 的 patch 版本）、**链接/代码布局** 上若有不同，也可能导致同一输入、不同结果。

**建议下一步**：

- **再提前**：在 `app_ai::Start()` 中**第一行**（在 `pool.Init`、队列、FaceRecognition 之前）调用 `CreateFaceDetectorEarly()`，看是否变为 0/0；若仍为 6/6，则基本可排除「本工程内创建顺序」。
- **对比配置与组件**：对比主工程与 ref 的 `sdkconfig`（尤其是 PSRAM、C++ 优化）和 `managed_components` 下 human_face_detect、esp-dl 的版本号与 CHECKSUMS，确认是否完全一致。
