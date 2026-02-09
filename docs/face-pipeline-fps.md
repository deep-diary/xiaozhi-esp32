# 人脸管道透传帧率评估

## 现象

透传模式（`FACE_AI_PASSTHROUGH=1`）下 log 显示约 **3.3 fps**（`FaceAI fps=3.3 (passthrough=1)`），明显低于相机标称或预期帧率。

## 主要原因

1. **DoCaptureOnly 的 3 次 DQBUF**  
   `main/boards/common/esp_video.cc` 中 `DoCaptureOnly()` 对相机做了 **3 轮** DQBUF，仅在第 3 次才把帧拷贝到 `frame_` 并返回。即每调用一次 `CaptureOnly()` 会消耗 3 帧相机输出，只得到 1 帧。  
   - 若相机实际输出约 **10 fps**，则管道理论最大约 **10/3 ≈ 3.3 fps**，与观测一致。  
   - 若相机为 24 fps，理论最大约 8 fps；实际还可能受下游消费或 buffer 不足影响。

2. **Buffer 不足丢帧**  
   `FaceCameraTask` 在 `TakeBuffer()` 失败时直接丢本帧并 `vTaskDelay(1)`。池只有 2 块 buffer（`FACE_QUEUE_FRAME_POOL_SIZE=2`），若主循环 `TickDisplay` 或 AI 任务消费稍慢，容易拿不到 buffer，进一步拉低有效帧率。

## 可选优化

- **减少 DQBUF 次数**：若不需要「丢弃前 2 帧」的防抖逻辑，可在 `DoCaptureOnly()` 中改为 1 次 DQBUF 即返回，管道帧率可接近相机输出（需评估是否有画面抖动或时序问题）。  
- **加大池/队列**：适当增大 `FACE_QUEUE_FRAME_POOL_SIZE`、`FACE_QUEUE_RAW_DEPTH`、`FACE_QUEUE_AI_DEPTH`，减少因 buffer 不足导致的丢帧。  
- **采集侧统计**：在 `FaceCameraTask` 中统计每秒成功入队帧数与丢帧数，便于区分「采集慢」与「拿不到 buffer」哪种为主。

## 参考

- 人脸检测组件输入与延迟：`managed_components/espressif__human_face_detect/README.md`（MSR 输入 120×160×3，延迟表为模型侧）。
- 规划与阶段：`.cursor/plans/人脸检测识别规划（human_face_detect_recognition）_36513e92.plan.md`。
