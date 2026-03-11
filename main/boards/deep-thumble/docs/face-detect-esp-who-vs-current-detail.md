# 人脸检测：esp-who 参考示例 vs 当前项目 详细对比

## 1. human_face_detect 组件版本

| 项目 | 参考 esp-who（ref 目录下） | 当前 xiaozhi-esp32 |
|------|----------------------------|---------------------|
| 组件路径 | `ref/esp-who/.../managed_components/espressif__human_face_detect` | `managed_components/espressif__human_face_detect` |
| **version** | **0.3.0** | **0.2.3** |
| esp-dl 依赖 | `~3.2.0` | `^3.1.3` |
| repository_info | commit_sha: dc380d45... | commit_sha: 9af7f980... |

结论：**版本不一致**。参考示例用 0.3.0，当前工程用 0.2.3。0.3.0 的 MSR/MNP 构造函数带 `score_thr`/`nms_thr`，且 MSRPostprocessor 构造多传了 `m_image_preprocessor`，后处理/阈值行为可能与 0.2.3 有差异。

---

## 2. 原始摄像头采集：分辨率与格式

| 项目 | 参考 esp-who | 当前 xiaozhi-esp32 |
|------|----------------|---------------------|
| 相机驱动 | **esp_camera**（WhoS3Cam 封装） | **EspVideo / V4L2-DVP**（config.json 等） |
| 采集分辨率 | `get_cam_frame_size_from_lcd_resolution()` → 与 BSP LCD 一致；Sparkbot 为 **FRAMESIZE_240X240**（board_config.h） | **240×240**（与 FACE_QUEUE_FRAME_WIDTH/HEIGHT、相机 config 一致） |
| 采集格式 | **PIXFORMAT_RGB565**，WhoS3Cam 直接配置 `camera_config.pixel_format = PIXFORMAT_RGB565`，传感器输出经 esp_camera 得到 RGB565 | 相机管线为 **YUYV** 或 **RGB565**（视 config）；队列里 `QueuedFrame.format`：1=RGB565，3=YUYV |
| 送入检测前的图像 | **240×240 RGB565**，来自 `camera_fb_t` → `cam_fb_t`，**无格式转换、无 resize** | **240×240**：若 format=3 则在 `RunFaceDetectCore` 内用 esp_imgfx 做 **YUYV→RGB565_LE** 再检测；若 format=1 则已是 RGB565 |

结论：分辨率一致（均为 240×240）。格式与数据路径不同：参考是 **esp_camera 直出 RGB565** 再进检测；当前是 **DVP/EspVideo 的 YUYV 或 RGB565**，且 YUYV 时经 esp_imgfx 转为 **RGB565_LE**。组件内部预处理器为 **RGB565_BIG_ENDIAN**，字节序/驱动差异可能导致误检。

---

## 3. 模型初始化与是否通过 load_model 加载

### 参考 esp-who（human_face_detect 0.3.0）

- **调用处**：`who_recognition_app_lcd.cpp`  
  `m_recognition->set_detect_model(new HumanFaceDetect(CONFIG_DEFAULT_HUMAN_FACE_DETECT_MODEL, false));`
- **构造函数**：`HumanFaceDetect(model_type_t model_type, bool lazy_load = true)`  
  - `lazy_load == false` → 在构造函数内**直接调用 `load_model()`**，不延迟。
- **load_model()**（在 ref 的 `human_face_detect.cpp` 内）：  
  - 根据 `m_model_type` 创建 `human_face_detect::MSRMNP`：  
    `new MSRMNP("human_face_detect_msr_s8_v1.espdl", m_score_thr[0], m_nms_thr[0], "human_face_detect_mnp_s8_v1.espdl", m_score_thr[1], m_nms_thr[1])`  
  - 即 **通过 load_model() 加载模型**，且 MSR/MNP 使用可配置的 `score_thr`、`nms_thr`（默认 0.5）。

### 当前 xiaozhi-esp32（human_face_detect 0.2.3）

- **调用处**：`face_detect_core.cpp`  
  `s_detector = new HumanFaceDetect();`  
  - 无参构造，使用默认 `model_type_t(CONFIG_DEFAULT_HUMAN_FACE_DETECT_MODEL)`。
- **构造函数**：`HumanFaceDetect(model_type_t model_type)`  
  - **没有** `lazy_load` 参数，**没有** `load_model()` 方法暴露给应用；组件内部在构造时**直接**创建 `MSRMNP(msr_model_name, mnp_model_name)`。
- **内部实现**：  
  - `HumanFaceDetect` 继承 `DetectWrapper`，构造时即 new 出 `human_face_detect::MSRMNP`。  
  - MSR/MNP 构造仅传模型名，**score/nms 阈值写死**（如 0.5、0.5），无 set_score_thr/set_nms_thr 暴露。

结论：参考示例**有**明确的 `load_model()` 流程（lazy_load=false 时在构造里调用），且 0.3.0 的 MSRMNP 支持可配置阈值；当前项目用的是 0.2.3，**没有** load_model 这一层抽象，模型在 `HumanFaceDetect()` 构造时**直接加载**，且阈值为组件内固定值。

---

## 4. 人脸检测流程对比

### 参考 esp-who

1. **输入图像**  
   - 来自 WhoFetchNode：`cam_fb_get()` → `camera_fb_t` 转为 `cam_fb_t`（who_cam_define.hpp）。  
   - 格式：**RGB565**，宽高由 `resolution[frame_size]` 决定（240×240）。
2. **构造 img_t**  
   - `WhoDetect::task()` 中：`auto fb = m_frame_cap_node->cam_fb_peek();`  
   - `dl::image::img_t img = static_cast<dl::image::img_t>(*fb);`  
   - `cam_fb_t::operator dl::image::img_t()` 填：`.data=buf, .width, .height, .pix_type=DL_IMAGE_PIX_TYPE_RGB565`（若 format 为 RGB565）。  
   - **无拷贝、无字节对调、无转 RGB888**。
3. **模型**  
   - 已通过 `set_detect_model(new HumanFaceDetect(..., false))` 设置，内部已 `load_model()` → MSRMNP。
4. **运行**  
   - `auto &res = m_model->run(img);`  
   - 结果可选 rescale（set_rescale_params），再通过 result_cb 交给画框/识别。

### 当前 xiaozhi-esp32

1. **输入图像**  
   - 来自 `QueuedFrame* qframe`（双队列 q_raw/q_ai 的帧）：宽高 240×240，`format` 1=RGB565、3=YUYV。
2. **格式与预处理**  
   - 若 `format == 3`：在 `RunFaceDetectCore` 内用 `esp_imgfx_color_convert` **YUYV → RGB565_LE**，写回 `qframe->data`，并设 `format=1`。  
   - 若 `format != 1` 且非 YUYV 则跳过检测。  
   - 若 `FACE_DETECT_USE_RGB888=1`：再分配 w×h×3，从当前 RGB565 按 LE 解析转成 **RGB888**，`run_img.pix_type=DL_IMAGE_PIX_TYPE_RGB888`；否则 `run_img.pix_type=DL_IMAGE_PIX_TYPE_RGB565`。  
   - 若 `FACE_DETECT_RGB565_BYTE_SWAP=1`（且未用 RGB888）：对副本做每像素高/低字节对调再送入 run。
3. **模型**  
   - 首次调用时 `s_detector = new HumanFaceDetect();`，无参，内部直接创建 MSRMNP（0.2.3 无 load_model 接口）。
4. **运行**  
   - `auto &raw_results = s_detector->run(run_img);`  
   - `rescale_and_filter(raw_results, ...)` 映射到帧尺寸并写入 `out_results`。

总结表：

| 步骤 | 参考 esp-who | 当前 xiaozhi-esp32 |
|------|----------------|---------------------|
| 输入格式 | 240×240 RGB565（esp_camera 直出） | 240×240，YUYV 或 RGB565；YUYV 时先转 RGB565_LE |
| 送入 run 的 img | `img_t` 直接来自 fb，**RGB565，无预处理** | RGB565 或 RGB888（可选）；可选 RGB565 字节对调 |
| 模型初始化 | `HumanFaceDetect(..., false)` → `load_model()` → MSRMNP(..., score_thr, nms_thr) | `new HumanFaceDetect()` → 内部直接 MSRMNP(msr, mnp)，固定阈值 |
| 运行 | `m_model->run(img)` | `s_detector->run(run_img)` |

---

## 5. FACE_DETECT_USE_RGB888=1 后的现象与建议

你提供的 log（FACE_DETECT_USE_RGB888=1 时）仍为：

- 每帧约 **5～10 个人脸**，无人脸时也类似；
- box0 多为 **`[0, y, 38~41, y+h]`**，即 **x 经常为 0**，框集中在左侧。

说明 **仅改为 RGB888 输入后，与之前 RGB565/byte_swap 相比“没有太大区别”**，误检与框偏左现象仍存在。可能含义：

1. **问题不单是 RGB565 字节序**：若纯字节序问题，转成 RGB888 后颜色应正确，误检应明显减少；未减少则可能还有：  
   - 传感器/驱动实际输出与 240×240 对齐、stride、或裁剪方式与组件假设不一致；  
   - 或 0.2.3 与 0.3.0 在 MSR/MNP 后处理上的差异（如 NMS/阈值/坐标映射）。
2. **组件版本差异**：参考示例在**同板/同 240×240 RGB565** 下检测正常，且参考用的是 **0.3.0**（MSRPostprocessor 等接口不同）。建议在条件允许时尝试**将当前工程升级到 human_face_detect 0.3.0**，并沿用参考的构造方式（如 `HumanFaceDetect(model_type, false)` 及 load_model 逻辑），看现象是否改善。
3. **固定测试图**：用一张标准人脸图（如从 ref 或 esp-dl 示例截取）生成 240×240 RGB565/RGB888，固定写入 `run_img.data` 再跑检测，可排除相机/管线差异，确认是否为“输入内容/布局”导致。

---

## 6. 小结表

| 对比项 | 参考 esp-who | 当前 xiaozhi-esp32 |
|--------|----------------|---------------------|
| human_face_detect 版本 | 0.3.0，esp-dl ~3.2.0 | 0.2.3，esp-dl ^3.1.3 |
| 采集分辨率 | 240×240 | 240×240 |
| 采集格式 | RGB565（esp_camera） | YUYV 或 RGB565（DVP/EspVideo） |
| 送入检测的格式 | 240×240 RGB565，直连 | 240×240 RGB565（或 YUYV 转 RGB565_LE）；可选 RGB888 或 RGB565 字节对调 |
| 模型是否经 load_model | 是（lazy_load=false 时在构造内调用） | 否，构造时直接创建 MSRMNP |
| 阈值 | MSRMNP 可配置 score_thr/nms_thr | 组件内固定 0.5 等 |
| run(img) 输入 | fb → img_t，无预处理 | qframe → (可选转换) → run_img |

建议后续：  
- 优先尝试**升级到 human_face_detect 0.3.0** 并对照 ref 的初始化与 load_model 用法；  
- 同时用**固定人脸图**做一次隔离测试，区分是“相机/管线/字节序”还是“组件版本/后处理”导致的问题。
