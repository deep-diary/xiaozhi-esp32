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

## 2. 框坐标顺序：应使用 FACE_DETECT_BOX_SWAP_XY=0

**依据**：

- `managed_components/espressif__esp-dl/vision/detect/dl_detect_define.hpp` 中 `result_t` 注释：  
  `box` 为 **[left_up_x, left_up_y, right_down_x, right_down_y]**，即 **[x0, y0, x1, y1]**。
- `human_face_detect` 组件内 MNP::run 使用 `center_x = (box[0]+box[2])>>1`、`center_y = (box[1]+box[3])>>1`，即 box[0],box[2] 为 x，box[1],box[3] 为 y。

因此 **ESP-DL 输出是标准 [x0, y0, x1, y1]**，我们应使用 **FACE_DETECT_BOX_SWAP_XY=0**。  
若误设为 1，会把 (x0,y0) 解析成 (y0,x0)，导致 log 里出现「全 (x,0) 或全 (0,y)」、框错位。

---

## 3. 输入图像格式与 RGB565 字节序（可能根因）

**当前链路**：

1. **相机**：Capture + GetLastFrame → `frame.format`（如 3=YUYV）→ 下采样 320×240 入队。
2. **app_face_detection**：若 format=3，用 `esp_imgfx_color_convert` 转成 **RGB565_LE** 写回 `qframe->data`。
3. **HumanFaceDetect::run(img_t)**：`img_t` 为 `DL_IMAGE_PIX_TYPE_RGB565`，组件内部用 **ImagePreprocessor**，且写死为 **DL_IMAGE_CAP_RGB565_BIG_ENDIAN**（见 `human_face_detect.cpp` 非 P4 分支）。

**风险**：

- **human_face_detect** 的 ImagePreprocessor 按 **RGB565 BIG_ENDIAN** 从 16 位像素里拆 R、G、B（见 esp-dl `dl_image_define.hpp` / `dl_image_color.hpp`）。
- 我们提供的是 **esp_imgfx 输出的 RGB565_LE**；若实际字节序/位布局与组件假设不一致，模型会看到**错误的颜色**，导致：
  - 误检（如天花板当脸）、漏检、框集中在某一侧等。

**建议**：

1. **先确认框顺序**：将 `FACE_DETECT_BOX_SWAP_XY` 设为 **0**，看屏上框是否包住人脸、log 里 (x,y) 是否都有合理变化。
2. **若仍异常**：在调用 `run(img)` 前，对 `qframe->data` 做一次 **RGB565 字节序转换**（例如每像素 `uint16_t` 高/低字节对调），再送入检测，看效果是否明显改善，以验证是否为 RGB565 字节序/格式不一致导致的根因。
3. **相机 format**：确认 `CameraFrame.format` 与 `task_face_camera` 下采样后写入的 `qframe->format` 一致；若相机本身出 RGB565，需确认是 LE 还是 BE，与组件假设对齐。

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
