# 人脸检测 log 简要分析（deep-thumble）

## 1. 模型是否正常加载

你提供的 log 中有：

```text
W (1485) dl::Model: Minimize() will delete variables not used in model inference...
W (1515) dl::Model: Minimize() will delete variables not used in model inference...
I (1515) FaceRecognition: ESP-DL HumanFaceDetect initialized (no face / blocked = no report).
```

- **两条 Minimize() 警告**：对应 **两个模型**（MSR + MNP 两阶段），说明 **人脸检测模型已正常加载**，不是“没加载”导致的问题。
- **HumanFaceDetect initialized**：说明 HumanFaceDetect（内部 MSR+MNP）初始化成功。

结论：**人脸检测模式是正常加载的**。对着天花板仍检测到 5–6 张脸，主要是 **模型对纹理/天花板误检**，而不是模型未加载。

---

## 2. 对着天花板仍 5–6 张脸的原因

- 组件内 MSR/MNP 的 score 阈值是 **0.5**，对天花板、墙面纹理等容易给出 0.5–0.7 的分数。
- 之前 **FACE_DETECT_SCORE_THRESHOLD=0.65**、**FACE_DETECT_MIN_BOX_SIZE=80** 仍不足以过滤掉这些误检。

本次已做：

- **FACE_DETECT_SCORE_THRESHOLD** 提高到 **0.75**（天花板仍多时可再改为 0.8）。
- **FACE_DETECT_MIN_BOX_SIZE** 提高到 **100**，只保留更大的框，进一步减少天花板误检。

---

## 3. 蓝色人脸框在左侧、不符合实际

可能原因：

1. **显示旋转**：预览在 240×240 上若被驱动做了 90° 旋转，而我们按 640×480 的 (x,y) 画框，框会错位。
2. **框坐标顺序**：若模型/后处理返回的是 **[y1, x1, y2, x2]** 而我们按 **[x1, y1, x2, y2]** 解析，会变成“竖条、在左侧”的效果。

本次已做：

- 在 **config.h** 增加 **FACE_DETECT_BOX_SWAP_XY**（默认 **1**）：按 **[y, x, y, x]** 解析框，用于修正“框在左侧、细竖条”的错位。
- 若改后框仍错位，可改为 **0** 试标准 [x,y,x,y]；若显示有 90° 旋转，再考虑在绘制时对框做旋转变换。

---

## 4. 屏幕“十来张后不再刷新”的可能原因

- **与“注册满 5/5”无直接关系**：注册满后只是替换最久未见的槽位，不会多占大块内存；预览缓冲是每周期分配、显示完即释放。
- **更可能是 PSRAM 紧张或碎片**：每帧预览需约 600KB PSRAM（640×480×2），多周期运行后若 PSRAM 碎片化或总余量不足，`heap_caps_malloc(..., PSRAM)` 会失败，预览不再刷新。
- **如何确认**：把 log 级别调到 **DEBUG**，看是否出现 `Test preview alloc failed (low memory), skip this cycle.`；或临时把该条从 `ESP_LOGD` 改为 `ESP_LOGW` 再跑一次，若停刷时出现该 WARN，即可确认是预览分配失败。
- **缓解**：内存紧张时已会跳过 Explain 保留 PSRAM；若仍停刷，可尝试减小预览分辨率（例如 320×240）或降低预览刷新频率（如每 5 周期显示一次）。

---

## 5. 本次配置与代码改动小结

| 项目 | 说明 |
|------|------|
| **FACE_USE_VIRTUAL_ONLY=1** | 新注册时 **不调 Explain**，只用虚拟姓名，先验证离线检测与框是否正常。 |
| **FACE_DETECT_SCORE_THRESHOLD=0.85** | 进一步提高置信度，减少天花板等误检（仍多可试 0.9）。 |
| **FACE_DETECT_MIN_BOX_SIZE=120** | 只保留 ≥120px 的框，进一步过滤小误检。 |
| **FACE_DETECT_BOX_SWAP_XY=1** | 框按 [y,x,y,x] 解析，修正“框在左侧、细竖条”的错位。 |
| **注册时打印已注册列表** | 格式：`Registered list: 【张三，李四，王五】 3/5`；每 20 次“Face seen again”也会打一次。 |

验证顺序建议：

1. 保持 **FACE_USE_VIRTUAL_ONLY=1**，对着真人脸看：框是否包住脸、姓名是否用虚拟名正常注册。
2. 对着天花板看：检测数量是否明显减少（0–1 个或 0 个）。
3. 若框仍错位，把 **FACE_DETECT_BOX_SWAP_XY** 改为 **0** 再试；或根据实际显示旋转再调绘制坐标。
4. 离线验证没问题后，将 **FACE_USE_VIRTUAL_ONLY** 改为 **0** 再走 Explain。
