# 人脸检测：参考示例 esp-who vs 当前项目差异对比

同硬件（deep-thumble / OV3660）下，ref 中 `ref/esp-who/examples/human_face_recognition` 人脸检测+5 关键点正常，当前项目出现「7～10 个框、未对人脸也大量检测」等不可靠结果。以下对比两边的配置与调用，便于排查根因。

## 1. 相机与图像来源（差异最大）

| 项目 | 参考 esp-who | 当前 xiaozhi-esp32 |
|------|----------------|---------------------|
| 相机驱动 | **esp_camera**（WhoS3Cam：esp_camera_init / esp_camera_fb_get） | **EspVideo / V4L2-DVP**（config.json OV3660_DVP_RGB565_240X240_24FPS） |
| 帧格式 | PIXFORMAT_RGB565，直接来自 camera_fb_t | RGB565（format=1），来自 CaptureOnly → GetLastFrame → 拷贝入队 |
| 送入 run(img) 的数据 | `img = static_cast<img_t>(*fb)`，**无拷贝、无字节对调** | `run_img.data = qframe->data` 或 **字节对调后的副本**（FACE_DETECT_RGB565_BYTE_SWAP=1） |
| 分辨率 | get_cam_frame_size_from_lcd_resolution() → 与 BSP LCD 一致（如 240×240） | 240×240，与 FACE_QUEUE_FRAME_* 一致 |

结论：参考示例使用 **esp_camera** 的 RGB565 缓冲直接给检测；当前使用 **DVP/EspVideo** 的缓冲。同一颗 OV3660 在不同驱动/管线下的 **RGB565 字节序（LE/BE）可能不同**，组件内部 ImagePreprocessor 固定为 **DL_IMAGE_CAP_RGB565_BIG_ENDIAN**，若当前管线实际输出为 LE，必须对检测输入做**每像素高/低字节对调**（即 FACE_DETECT_RGB565_BYTE_SWAP=1），否则模型会看到错误颜色，导致误检/多框/不可靠。

## 2. 检测组件与 API

| 项目 | 参考 esp-who | 当前 xiaozhi-esp32 |
|------|----------------|---------------------|
| 组件 | espressif/human_face_detect（含 human_face_recognition 依赖） | 同 espressif__human_face_detect |
| 构造 | `new HumanFaceDetect(model_type_t(CONFIG_...), false)`（第二参数或为封装层用途） | `new HumanFaceDetect()`（无参，使用 CONFIG_DEFAULT_HUMAN_FACE_DETECT_MODEL） |
| 调用 | `auto &res = m_model->run(img);`，img 即 fb 转成的 img_t | `auto &raw_results = s_detector->run(run_img);`，run_img 为 data/width/height/pix_type=RGB565 |
| 结果处理 | rescale_detect_result（仅当设置了 rescale 参数时）；画框/关键点用 result_t.box 与 keypoint | rescale_and_filter：坐标按帧尺寸缩放并 clamp，无额外阈值/最小框 |

结论：两边都是 **HumanFaceDetect → run(img_t)**，img_t 均为 **data + width + height + pix_type=DL_IMAGE_PIX_TYPE_RGB565**。API 用法一致；差异仅在**图像内存来源与字节序**（见上节）。

## 3. 模型与配置

| 项目 | 参考 esp-who | 当前 xiaozhi-esp32 |
|------|----------------|---------------------|
| 模型 | CONFIG_DEFAULT_HUMAN_FACE_DETECT_MODEL，MSRMNP_S8_V1 | 同，CONFIG_HUMAN_FACE_DETECT_MSRMNP_S8_V1、MODEL_IN_FLASH_RODATA |
| 输入尺寸 | 由组件内部 resize 到 MSR 120×160 | 同，不在此处 resize，直接 240×240 进 run() |
| 阈值/过滤 | 未在应用层做 score/min_box 过滤，完全用组件输出 | 已去掉阈值/最小框，完全采用组件输出 |

结论：模型加载与输入尺寸处理一致，无差异。

## 4. 参考示例中与检测强相关的配置摘要

- **config.h / board_config.h**：DISPLAY_SWAP_BYTES=0、DISPLAY_RGB_ENDIAN_BGR=0；仅影响显示，不改变送入检测的 fb。
- **frame_cap_pipeline**：WhoS3Cam(PIXFORMAT_RGB565, frame_size, MODEL_TIME+3) 或 (..., true, true)（Korvo2）；无 Decode/Resize，Fetch 直接出 RGB565 fb。
- **WhoDetect::task()**：cam_fb_peek() → `img = static_cast<img_t>(*fb)` → `m_model->run(img)`，无任何预处理或字节交换。

## 5. 当前项目已做/建议

- **FACE_DETECT_BOX_SWAP_XY=0**：保持与 ESP-DL result_t [x0,y0,x1,y1] 一致；交换只会把「框在左」变成「框在上」，不解决检测不可靠。
- **FACE_DETECT_RGB565_BYTE_SWAP=1**：当前 DVP/EspVideo 输出为小端时，检测前对副本做每像素高/低字节对调再送入 run()，使组件（BIG_ENDIAN 预处理器）看到正确颜色，**这是与参考示例差异最大且最可能影响结果的一环**。
- 若开启 BYTE_SWAP=1 后仍异常，可再核对：相机实际输出格式（是否确为 RGB565）、EspVideo 在 240×240 RGB565 下是否仍有内部转换或 stride 与组件假设不一致等。

## 6. 小结

- 参考示例与当前项目在 **HumanFaceDetect API、模型、输入尺寸** 上一致。
- 主要差异在 **相机与图像路径**：参考用 **esp_camera** 的 RGB565 直连检测且不做字节对调；当前用 **EspVideo/DVP** 的 RGB565，且组件期望 BIG_ENDIAN，故需 **FACE_DETECT_RGB565_BYTE_SWAP=1** 保证检测输入字节序与组件一致，否则会出现大量误检/不可靠检测。
