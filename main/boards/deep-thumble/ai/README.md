# deep-thumble AI 模块（esp-who 风格）与架构评估

本目录为从 esp-who / human_face_recognition_no_wakenet 复制的人脸检测等模块（who_human_face_detection、who_ai_utils 等）。下面评估**双队列 + 双任务**架构，并与当前 face 目录下的“单任务周期 RunOnce”方式对比。

---

## 一、双队列 + 双任务架构（你贴的伪代码）

```
Camera 任务: esp_camera_fb_get() → xQueueSend(xQueueFrameO, &camera_frame)
                ↓
AI 任务:       xQueueReceive(xQueueFrameO) → infer(MSR+MNP) → draw_detection_result → xQueueSend(xQueueAIFrameO)
                ↓
LCD/显示:      xQueueReceive(xQueueAIFrameO) → 显示（代码中未贴，通常另有任务或主循环取队列显示）
```

**要点：**

- **xQueueFrameO**：只传“原始图像”（或 `camera_fb_t*`），相机任务只负责取帧、入队。
- **xQueueAIFrameO**：传“AI 处理后的同一帧”（画完框或未画框），无论是否检测到人脸都入队，保证显示端每帧都有图可刷。
- **数据流**：相机 → 队列 → AI（消费一帧、处理、再入队）→ 队列 → 显示；职责清晰，易于理解。

**优点：**

1. **解耦**：相机按自身节奏取帧；AI 按自身节奏消费；显示按自身节奏取“已处理帧”。互不阻塞（在队列未满/未空时）。
2. **流水清晰**：谁生产、谁消费、谁再生产，一目了然。
3. **同一 buffer 复用**：AI 在收到的 `camera_fb_t*` 上直接画框，再送入显示队列，无需为“预览”再拷贝一整帧（在 esp_camera 的 fb 池子模型下）。
4. **“无论是否检测到都送显示”**：逻辑简单，不会出现“无脸就不刷新”的歧义。

**需要注意的点：**

1. **buffer 所有权与生命周期**：  
   - 谁从队列里取出 `camera_fb_t*`，谁就负责在用完后 `esp_camera_fb_return()`（或按项目约定归还/释放）。  
   - 通常：相机任务只负责 get + 入队；AI 任务 receive → 处理 → 入队（同一指针）；显示任务 receive → 显示 → **esp_camera_fb_return**。若没有“显示任务”或显示不还 buffer，需要明确由 AI 任务在 send 之后不再持有，由接收端归还。
2. **队列深度**：  
   - 队列深度 5、任务优先级和栈大小要匹配帧率与处理耗时，避免队列爆满或 AI 饿死。
3. **与当前 xiaozhi 的 Camera 抽象不一致**：  
   - 当前项目用的是 `Camera*`（如 Esp32Camera）+ `Capture()` + `GetLastFrame()`，没有 `esp_camera_fb_get/return` 的“fb 池”。  
   - 若采用双队列架构，要么：  
     - 在相机侧封装一层“取一帧 → 放入我方 buffer 或包装成可入队结构 → 入队”，并约定 buffer 由谁分配、谁释放；要么  
     - 引入类似 esp_camera 的 fb 池，由相机任务 get、AI 用、显示用后 return。

---

## 二、与当前 face 目录实现的对比

| 维度           | 当前 face（单任务周期 RunOnce）              | 双队列 + 双任务（esp-who 风格）                    |
|----------------|----------------------------------------------|----------------------------------------------------|
| 任务模型       | 一个 FaceRecognitionTask，每 N ms 调 RunOnce | 相机任务 + AI 任务（+ 可选显示任务）               |
| 取帧           | 在 RunOnce 里 `Capture()` + `GetLastFrame()` | 相机任务独立 `esp_camera_fb_get()` 入队            |
| 检测与绘制     | RunOnce 内：resize → detect → 过滤 → 画框   | AI 任务：收帧 → infer → draw_detection_result     |
| 显示           | RunOnce 末尾 `ShowTestPreviewIfNeeded` 拷贝一帧送 LVGL | AI 任务将“同一帧”指针入队，显示端从队列取帧显示 |
| 内存           | 每周期可能分配 640×480×2 做预览拷贝          | 可零拷贝（同一 fb 流转），依赖 fb 池/约定          |
| 相机与 MCP 等  | 与 MCP 拍照等共用 Camera，需注意并发         | 相机任务独占 esp_camera，与其它模块通过队列交互    |

当前方式的优点：和现有 xiaozhi 的 Camera/Display 接口一致，改造成本小。缺点：预览要额外拷贝一帧，易受 PSRAM 碎片与占用影响（“多张图后不刷新”）；取帧与检测在同一任务内，节奏绑在一起。

双队列方式的优点：数据流清晰、易扩展（加识别、加滤镜等只需在“AI 任务”里加一步）；显示端逻辑简单（有帧就显示）；在 fb 池模型下可避免预览的大块拷贝。缺点：要接好当前项目的 Camera/Display，并设计好 buffer 所有权（或引入 fb 池）。

---

## 三、若在 deep-thumble 中采用双队列架构，建议步骤

1. **定义“帧”在项目中的表示**  
   - 若继续用现有 `Camera*`：可取一帧后拷贝到一块 PSRAM buffer，将“指针 + 宽高 + 格式”打包成结构体入队；约定该 buffer 由显示端或专门任务在用完后释放，或归还到项目内的“帧池”。  
   - 若改用 `esp_camera_fb_get/return`：则与参考代码一致，队列里传 `camera_fb_t*`，由最后使用者 `esp_camera_fb_return`。

2. **相机任务**  
   - 只做：取一帧（get 或 Capture+GetLastFrame 并拷贝到可入队 buffer）→ `xQueueSend(xQueueFrameO, ...)`。  
   - 不参与检测、不参与显示。

3. **AI 任务**  
   - `xQueueReceive(xQueueFrameO)` → 若使用 HumanFaceDetect（或你复制的 MSR01+MNP01）做 infer → 在**同一 buffer**上 `draw_detection_result`（或当前 face 的 DrawFaceBoxesOnRgb565）→ `xQueueSend(xQueueAIFrameO, ...)`。  
   - 无论是否检测到人脸，都送出一帧，保证显示不“断流”。

4. **显示端**  
   - 从 `xQueueAIFrameO` 取帧（或指针），调用当前项目的 Display/LVGL 接口显示；显示完后按约定归还 buffer 或 `esp_camera_fb_return`。  
   - 可与现有 LVGL 任务或主循环结合（例如在 LVGL tick 或 10ms 循环里非阻塞地 `xQueueReceive(..., 0)`，有帧就更新显示）。

5. **队列深度与栈**  
   - 参考你贴的 5 帧队列、相机 4KB、AI 6KB 起步；若用 HumanFaceDetect 或更大模型，可适当加大 AI 任务栈。

6. **与 MCP 拍照等共存**  
   - 若相机任务独占硬件，MCP 拍照需通过“向相机任务发请求 + 另一队列取结果”等方式串行化；若仍用当前 Camera 抽象，则需在 Capture/GetLastFrame 处加互斥，避免与队列驱动的取帧冲突。

---

## 四、结论与建议

- **架构上**：双队列 + 双任务（相机 → 原始帧队列 → AI → 处理后帧队列 → 显示）思路清晰，和 esp-who 参考代码一致，适合“取帧 → 检测 → 画框 → 显示”这条管线，也便于以后加识别、滤镜等。
- **在 deep-thumble 里采用**：需要把“帧”的获取与所有权与当前 Camera/Display 对齐（要么用 esp_camera fb 池，要么自建帧池/拷贝 + 约定释放者），并实现显示端从 `xQueueAIFrameO` 取帧并刷新屏幕；其余（AI 任务里 infer + draw，无论是否检测到都送一帧）可直接沿用你贴的逻辑。
- **建议**：若你希望人脸检测与显示管线更清晰、且愿意改一层相机/显示对接，可以按上述步骤在 deep-thumble 里接一套“双队列 + 双任务”；若优先稳定当前逻辑，可保留 face 目录单任务方案，仅把“无论是否检测到都刷新一帧”和“遮挡时也送一帧”等策略借鉴过来，减少“不刷新”和“挡住还看到旧框”的问题。

本目录下的 who_human_face_detection 等实现可与该双队列架构配合使用：由调用方（例如新建的 ai_task）从 xQueueFrameI 取帧、调用现有 infer + draw、再送入 xQueueFrameO 即可。
