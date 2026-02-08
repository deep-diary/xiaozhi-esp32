# Esp32Camera 类说明与采集/显示分离方案

## 一、Esp32Camera 类简要说明

### 1.1 核心成员与含义

| 成员 | 含义 |
|------|------|
| `frame_` | 存放**当前/最后一帧**：`data`（PSRAM）、`len`、`width`、`height`、`format`（如 YUYV/RGB565） |
| `video_fd_` | 打开的 V4L2 设备 fd（如 `/dev/video2`，DVP） |
| `streaming_on_` | 是否已开启流（VIDIOC_STREAMON） |
| `mmap_buffers_` | 驱动 mmap 的环形缓冲，Capture 时 DQBUF 取一帧再 QBUF 还回 |
| `capture_preview_enabled_` | 为 true 时，Capture() 末尾会把 `frame_` 显示到屏幕（LvglDisplay::SetPreviewImage） |
| `explain_capture_mutex_` | 串行化 Capture 与 Explain，避免编码线程与取帧冲突 |

### 1.2 Capture() 在做什么（采集 + 显示）

当前 `Capture()` 的流程可以概括为：

1. 拿 `explain_capture_mutex_`
2. 若 `!streaming_on_ || video_fd_ < 0` 直接返回 false
3. **采集**：DQBUF 3 次（丢弃前 2 帧、取第 3 帧）→ 释放旧 `frame_.data` → 为当前帧 malloc 新 buffer → 从 `mmap_buffers_[buf.index]` 拷贝到 `frame_`（含格式转换、可选旋转）→ QBUF 还回
4. **显示（可选）**：若 `capture_preview_enabled_` 为 true，则用 `frame_` 调 `LvglDisplay::SetPreviewImage(...)` 做预览
5. 返回 true

所以：**采集和显示在同一个 Capture() 里**；显示可以通过 `SetCapturePreviewEnabled(false)` 关掉（deep-thumble 人脸管道已关，避免和 FaceDisplayTask 抢 PSRAM）。

### 1.3 存放相机结果的内存（frame_）

- **不是**“同一块物理内存从开机一直占着”：每次 `Capture()` 里都会对上一帧做 `heap_caps_free(frame_.data)`，再 `heap_caps_malloc(...)` 新 buffer 写当前帧。
- **逻辑上**：“最后一张照片”一直由 `frame_` 表示；在**两次 Capture() 之间**，这块内存一直存在且内容不变。
- 若每隔 1s 调用一次 Capture()，则每秒会：释放上一帧 buffer → 分配新 buffer → 写入新一帧 → （可选）预览。也就是说，**反复采集就是在不断更新“最后一张图”所在的那块内存**（实际是换新 buffer，旧的被 free）。

### 1.4 Explain() 用的是什么图

`Explain(question)` 内部：

- 先拿 `explain_capture_mutex_`
- **直接使用当前 `frame_.width / frame_.height / frame_.format / frame_.data`** 做 JPEG 编码并 HTTP 上传
- **不会**在内部再调一次 `Capture()`

因此 Explain 用的就是**当前类里缓存的最后一帧**。若之前从未调用过 Capture()，或设备 open 失败，`frame_.data` 可能为 nullptr，Explain 会异常或编码失败。

---

## 二、你的三个问题的直接回答

### 2.1 采集和显示分开、人脸检测流程

- 当前：Capture = 采集 + （可选）预览；人脸管道已通过 `SetCapturePreviewEnabled(false)` 关掉 Capture 内预览，由 FaceDisplayTask 自己显示。
- 若希望接口上“采集”和“显示”完全分离，可以：
  - 新增**只采集**：例如 `CaptureOnly()`，只做 DQBUF → 填 `frame_`，不做 SetPreviewImage。
  - 新增**只显示**：例如 `ShowLastFrame()`（把当前 `frame_` 显示到屏幕），或 `ShowBuffer(buf, w, h, fmt)`（显示任意 buffer，给人脸管道用）。
- 人脸检测流程可以变成：**采集（CaptureOnly）→ 取帧（GetLastFrame）→ 人脸任务处理（检测/画框）→ 显示（ShowBuffer 或现有 FaceDisplayTask 走 LCD）**。这样“采集”和“显示”在接口上就分开了。

### 2.2 方案 a 与 b 的评估

| 维度 | a) 直接在项目源码（common）上改 | b) 板级目录下新实现一个相机类 |
|------|----------------------------------|-------------------------------|
| 做法 | 在 `esp32_camera.cc/.h` 里新增 `CaptureOnly()`、`ShowLastFrame()` 或 `ShowBuffer()` | 在 `main/boards/deep-thumble/` 下写一个类（继承或包装 Camera），实现“采集、显示独立” |
| 优点 | 一处实现，所有板子一致；不重复 V4L2/DQBUF 逻辑；无额外缓冲则无额外内存 | 不改 common，只影响本板 |
| 缺点 | 动 common，需考虑其他板子/用例 | 若**完全**在板级实现采集：要重复 esp_video/V4L2 逻辑，且通常需要自己的 buffer，**会多占一份内存**（例如再一份 640×480×2）；若板级只是**包装** Esp32Camera 并调用“只采/只显”，则 common 仍要先提供 CaptureOnly/Show 等，否则板级拿不到“只采”能力 |
| 推荐 | **更合适**：在 common 里加 `CaptureOnly()` 和可选 `ShowLastFrame()`/`ShowBuffer()`，改动集中、无重复逻辑、内存模型清晰（仍只有一份 frame_） | 仅在不允许改 common 时考虑；且若选 b，建议 common 仍提供 CaptureOnly，板级做薄包装（这样不会多一份大 buffer） |

结论：**优先选 a**；若必须 b，也建议 common 先有 CaptureOnly，板级只做薄包装，避免重复逻辑和额外大块内存。

### 2.3 拍照 MCP：是否可以不 Capture 直接 Explain？

- **可以**：Explain() 用的是 `frame_` 里当前缓存的那一帧，不依赖本次调用前是否刚调过 Capture()。
- **语义区别**：
  - **先 Capture() 再 Explain(question)**：保证“当前这一刻”的图被上传解释，适合“拍一张并解释”。
  - **直接 Explain(question)**：用“最近一次被写入 frame_ 的图”（可能是人脸管道或其他逻辑之前采的），不保证是“刚拍的”，适合“用已有缓存图解释”、省一次采集。
- 若希望 take_photo 表示“**拍一张并解释**”，应保留先 `Capture()` 再 `Explain()`；若希望“**用已有缓存图解释**”（例如人脸管道一直在采，MCP 直接用最新一帧），可改为只调 `Explain(question)`，并在描述里说明是“用当前缓存的相机图像进行解释”。

---

## 三、若在 common 中实现“采集 / 显示分离”（方案 a 示例）

1. **CaptureOnly()**  
   - 逻辑与当前 `Capture()` 相同，但**去掉**末尾 `if (capture_preview_enabled_) { ... SetPreviewImage(...); }` 这一段。  
   - 可选：保留原 `Capture()` 为“采集+可选预览”，内部调用 CaptureOnly() 再根据 `capture_preview_enabled_` 决定是否 ShowLastFrame()。

2. **ShowLastFrame()**  
   - 若 `frame_.data` 有效，则把当前 `frame_` 交给 LvglDisplay::SetPreviewImage（即把现在 Capture 里那一段预览抽成单独函数）。  
   - 人脸管道可以：CaptureOnly() → GetLastFrame() → 检测 → 画框到副本 → 显示时要么调 ShowBuffer(副本)，要么仍由 FaceDisplayTask 直接写 LCD（不经过 SetPreviewImage）。

3. **ShowBuffer(buf, len, w, h, fmt)**（可选）  
   - 用于“显示任意 buffer”（例如人脸检测画框后的结果），避免必须把结果再拷回 `frame_`。  
   - 实现上可以是：构造一个临时 LVGL 图像或直接调 display 的接口显示这块 buffer。

4. **MCP take_photo**  
   - 若产品定为“用缓存图解释”：只调 `Explain(question)`，不调 Capture()。  
   - 若产品定为“拍一张并解释”：保持 `Capture()`（或 `CaptureOnly()`）+ `Explain(question)`。

这样，人脸检测流程可以是：**采集（CaptureOnly）→ 输出相机图到人脸任务 → 检测结束 → 用现有或新增的显示接口显示**；拍照 MCP 则按语义选择“先 Capture 再 Explain”或“直接 Explain 缓存图”。
