# 人脸检测模型路径与「全黑仍多框」排查

## 模型放在哪里、如何编进固件

- **源文件**：`managed_components/espressif__human_face_detect/models/s3/`
  - `human_face_detect_msr_s8_v1.espdl`（约 61KB）
  - `human_face_detect_mnp_s8_v1.espdl`（约 130KB）
- **打包**：CMake 在配置时（当 `CONFIG_FLASH_HUMAN_FACE_DETECT_MSRMNP_S8_V1=y`）用 esp-dl 的 `pack_espdl_models.py` 将上述两个文件打成 **一个** 二进制：
  - 输出：`build/espdl_models/human_face_detect.espdl`（约 **191KB**）
- **嵌入**：`target_add_aligned_binary_data(COMPONENT_LIB, packed_model, BINARY)` 把该 packed 文件转成 `.S` 并编进 **espressif__human_face_detect** 组件，生成符号：
  - `_binary_human_face_detect_espdl_start` / `_binary_human_face_detect_espdl_end`
- **加载**：运行时 `human_face_detect.cpp` 里 `path = (const char *)human_face_detect_espdl`（即上述 start 地址），`dl::Model(path, "human_face_detect_msr_s8_v1.espdl", ...)` 从 packed 里按名字取出 MSR/MNP 子模型。

**如何确认模型被正确编进去**：

1. 构建后看 `build/espdl_models/human_face_detect.espdl` 是否存在且约 191KB。
2. 运行后看 log：若出现 `FaceDetectCore: face model RODATA: 191248 bytes (packed msr+mnp, expect ~191KB)`，说明嵌入的 RODATA 大小正常。

## 全黑画面仍检测出很多框（模型「失效」）

现象：摄像头挡住、全黑时依然出现多个人脸框，说明**模型在跑，但输入被错误解释**，而不是模型文件没编进去。

常见原因与处理：

1. **RGB565 字节序与组件不一致**  
   组件 ImagePreprocessor 使用 `DL_IMAGE_CAP_RGB565_BIG_ENDIAN`，若相机给的是 Little Endian，预处理会把整幅图（包括全黑）解释错，容易在错误「图案」上触发大量 anchor。  
   - **建议**：在 `config.h` 中把 `FACE_DETECT_RGB565_BYTE_SWAP` 设为 **1**，对送入检测的副本做 LE→BE 再 `run(img)`，再观察全黑时框是否消失或明显减少。

2. **sdkconfig 未选模型**  
   - 确认：`CONFIG_FLASH_HUMAN_FACE_DETECT_MSRMNP_S8_V1=y`、`CONFIG_HUMAN_FACE_DETECT_MODEL_IN_FLASH_RODATA=y`（或你使用的存储方式）。  
   - 若未选，`load_model()` 不会创建 MSRMNP，运行时可能崩溃或行为异常，而不是「多框」；多框更偏向上面第 1 点。

3. **clean build 再测**  
   修改 sdkconfig 或组件后建议 `idf.py fullclean` 再 `idf.py build`，避免用旧的 packed/嵌入结果。

总结：模型路径和编译嵌入按上面检查即可；「全黑仍多框」优先试 **FACE_DETECT_RGB565_BYTE_SWAP=1**，并确认 RODATA 大小 log 约 191KB。

## 示例图与自检

- **esp-dl / human_face_detect 组件均不附带示例人脸图**，无法直接「用官方示例图」跑一次检测。
- 启动时会对 **全黑图（240×240 RGB565 全 0）** 做一次自检：`s_detector->run(black_img)`。
  - 若 log 为 `self-test black image: 0 face(s) ok`，说明模型与预处理在标准输入下正常，问题多半在**相机帧的格式/字节序/ stride**。
  - 若为 `self-test black image: N face(s) (expect 0; ...)`，说明全黑也被误检，需重点查输入管线（字节序、分辨率、对齐等）。
- 若要用「已知好人脸图」验证：可从 esp-who 示例跑一帧保存为 RGB565 二进制，或自行准备 240×240 RGB565 文件，在代码里加载后构造 `img_t` 再 `run(img)` 对比结果。
