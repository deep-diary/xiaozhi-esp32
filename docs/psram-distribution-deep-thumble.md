# PSRAM 分布表（deep-thumble 板型）

根据之前串口日志与代码中的 PSRAM 分配点整理，便于排查“显示几次后停刷”“Explain/JPEG 分配失败”等问题。

---

## 1. 日志中的内存信息

### 1.1 启动时

```text
I (160) esp_psram: Adding pool of 8192K of PSRAM memory to heap allocator
I (196) esp_psram: Reserving pool of 64K of internal memory for DMA/internal allocations
```

- **PSRAM 总量**：8192 KB = **8 MB**
- **64K 预留**：为 DMA/内部预留的是 **内部 RAM**，不占 PSRAM 池子

### 1.2 运行中（SystemInfo 周期打印）

```text
I (9066) SystemInfo: free sram: 78035 minimal sram: 53583
...
W (18616) SystemInfo: free sram: 54275 minimal sram: 36779
...
I (62616) SystemInfo: free sram: 39079 minimal sram: 18323
```

- **free sram / minimal sram**：来自 `esp_get_free_heap_size()` / `esp_get_minimum_free_heap_size()`，是 **内部 + PSRAM 的合并堆**，不区分具体是内部还是 PSRAM。
- **minimal sram 降到 ~18KB**：多出现在唤醒会话（listening/speaking）时，此时内部 RAM 紧张，JPEG 编码（`jpeg_calloc_align` 等）易失败；PSRAM 仍可能有余量，但总堆余量低时大块分配也会失败。

---

## 2. PSRAM 按业务逻辑的分布（估算）

| 业务模块 | 用途 | 单次/常驻大小（字节） | 何时分配 / 释放 | 说明 |
|----------|------|------------------------|------------------|------|
| **显示（LcdDisplay）** | LVGL 图片缓存 | **2 × 1024 × 1024 = 2 MB** | 初始化时，常驻 | `lv_image_cache_resize(2*1024*1024)`，PSRAM≥8MB 时使用 |
| **摄像头（Esp32Camera）** | 帧数据副本 | **614 400**（≈600 KB） | 每次 DQBUF 后分配，下一帧前释放 | `frame_.data`，640×480 YUYV = 640×480×2 |
| **摄像头** | 预览图（RGB565） | **614 400**（≈600 KB） | ShowPreviewImage 时，用完后由 LcdDisplay 接管或释放 | `w*h*2`，640×480×2；与人脸预览不同时占用 |
| **摄像头** | Explain JPEG 块 | **~15 KB × 块数**（通常 1 块） | Explain 流式编码时按块分配，发送后释放 | `heap_caps_aligned_alloc(16, len, PSRAM)`，len 为单块 JPEG 长度（日志约 15xxx） |
| **人脸（FaceRecognition）** | 测试预览缓冲 | **614 400**（≈600 KB） | 测试预览每 3 周期一次，SetPreviewImage 后由 Display 释放 | `ShowTestPreviewIfNeeded` 里 `heap_caps_malloc(size_rgb565, PSRAM)` |
| **人脸** | YUYV→RGB565 转换 | **614 400**（≈600 KB） | 每周期若 format=3 则分配，周期末 ConvBufGuard 释放 | `conv_buf`，与预览缓冲不同时存在 |
| **人脸** | 检测输入 160×120×3 | **57 600**（≈56 KB） | 每周期检测时，优先内部 RAM，失败再用 PSRAM | `dst_buf`，先 `MALLOC_CAP_INTERNAL` 再 `MALLOC_CAP_SPIRAM` |
| **JPEG（image_to_jpeg）** | 编码输出缓冲 | **≥ 128 × 1024**（128 KB） | Explain/拍照时，编码完成后释放 | `malloc_psram(out_cap)`，out_cap ≥ 128KB；640×480 时约 460800+64KB |
| **JPEG** | 输入转换（部分路径） | **0（PSRAM）** | 当前软编码用 `jpeg_calloc_align`，多为内部 RAM | 硬件编码路径会用 `malloc_psram` 的转换缓冲 |
| **唤醒词（AFE）** | 编码任务栈 | **24 576**（24 KB） | 初始化时，常驻 | `4096*6`，`heap_caps_malloc(stack_size, MALLOC_CAP_SPIRAM)` |

---

## 3. 峰值与冲突（为何“显示几次后停刷”）

- **常驻**：LVGL 缓存 **2 MB** + 唤醒词栈 **24 KB** ≈ **2.02 MB**。
- **同帧可能同时存在**（人脸测试预览 + Explain 同时跑时）：
  - 摄像头帧副本 600 KB  
  - 人脸预览缓冲 600 KB  
  - 人脸 YUYV 转换 600 KB（若本周期做了检测且 format=3）  
  - JPEG 输出 128 KB+  
  - Explain JPEG 块 ~15 KB  
  - 人脸检测输入 56 KB（若走 PSRAM）  
  → 单周期“人脸 + Explain”可再需约 **2 MB+** 的 PSRAM。
- **8 MB 总量** 下，扣除 2 MB 常驻、碎片和其他任务占用后，再连续多周期 **预览 + Explain** 会把可用 PSRAM 压得很低，导致：
  - `alloc out buffer failed`（JPEG 输出）
  - `Test preview alloc failed`（人脸预览）
  - 屏幕不再刷新（预览分配不到 600 KB）。

因此当前策略是：**PSRAM 剩余 &lt; 700 KB 时本周期不请求 Explain**，把余量留给预览，避免“显示几次后报错、屏幕停刷”。

---

## 4. 内部 RAM（与 PSRAM 区分）

- **minimal sram ~18 KB** 指的是 **内部 + PSRAM 总堆** 的最小余量，多被内部使用（任务栈、协议、音频等）压低。
- **JPEG 输入转换**（`jpeg_calloc_align`）在 `image_to_jpeg.cpp` 里多为 **内部 RAM**，因此“alloc/convert input failed”常与内部紧张有关；PSRAM 再省也无法直接缓解这部分。

---

## 5. 如何自行核对 PSRAM

在代码或调试中可加：

```c
#include <esp_heap_caps.h>

size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
size_t total_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
ESP_LOGI(TAG, "PSRAM: free=%zu total=%zu", free_psram, total_psram);
```

- 若需按“模块”统计，可在各模块分配/释放处打 `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` 日志，对比前后差值即该次分配占用（同一时刻仅该处变化时）。

---

## 6. 小结表（仅 PSRAM）

| 项目 | 大小（约） | 类型 |
|------|------------|------|
| PSRAM 总容量 | 8 MB | 设备 |
| LVGL 图片缓存 | **1 MB**（原 2 MB，已缩小以省 PSRAM） | 常驻 |
| 唤醒词编码栈 | 24 KB | 常驻 |
| 人脸帧池（pool=2） | **2 × 614 400 ≈ 1.2 MB**（原 4×≈2.4 MB） | 常驻池 |
| 摄像头帧副本 | 600 KB | 按帧 |
| 预览/人脸预览缓冲 | 600 KB × 1 | 按需 |
| 人脸 YUYV 转换 | 600 KB | 按周期（format=3） |
| JPEG 输出缓冲 | ≥128 KB | Explain/拍照 |
| Explain JPEG 块 | ~15 KB | 流式块 |
| 人脸检测输入（PSRAM 优先） | 153 600（320×240×2） | 按周期，优先 PSRAM |

以上为根据此前日志与代码整理的 PSRAM 分布；**内存降低措施**见 `main/boards/deep-thumble/SWRS_ai.md` 中「内存降低措施」一节（帧池/队列缩小、人脸 dst_buf 与 who rgb888 用 PSRAM、LVGL 缓存缩小等）。
