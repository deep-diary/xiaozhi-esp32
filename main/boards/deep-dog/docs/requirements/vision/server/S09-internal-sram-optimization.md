# V-S09 · Internal SRAM 优化评估与实施指南

| 项 | 内容 |
|----|------|
| 路线图 ID | **V-S09** |
| 优先级 | P1（稳定性 / 可扩展性；非阻塞当前功能交付） |
| 依赖 | [S04](./S04-local-face-numeric-id.md) · [S05](./S05-immich-real-name.md) · [S08](./S08-rtsp-face-wdt-mitigation.md) · [01-device MQTT](../../mqtt/modules/01-device.md) · [N01 SNTP](../../net/N01-sntp-clock-sync.md) |
| 代码落点 | `face_ai/*` · `esp_sparkbot_board.cc` · `mqtt/memory_report.*` · `sdkconfig.defaults.esp32s3` · `vision/vision_frame_hub.cc` · `http-server/*` |
| 基线状态 | **评估文档**（待 Agent 按 §6 顺序逐项落地） |
| 观测工具 | MCP `self.board.diagnostics` · MQTT `device/status` · `scripts/capture_deep_dog_baseline.sh` |

## 1. 背景

ESP32-S3 deep-dog（8MB PSRAM + ~400KB internal 池）在 **人脸 + 板级 MQTT + HTTP + WS MCP + VisionHub** 同开时，internal SRAM 长期处于低位。PSRAM 仍充裕（face_ready 后 ~4.5MB+ free），瓶颈在 **internal 总量、启动峰值、碎片化**，而非人脸库人数。

与官方 **esp-sparkbot**（无人脸/vision/MQTT）对比：激活后 internal free ~110KB；deep-dog face_ready 后仅 ~13KB free，`largest_free_block` 可低至 **3.5KB**，historical `min` 曾到 **231B**（模型加载瞬间）。

本文件供后续 Agent **按优先级逐项优化**，每项含目标、改动点、风险、验收；禁止未写入本文/ROADMAP 的大范围重构。

## 2. 基线数据（2026-08-14/15，v2.3.0 deep-dog）

日志：`main/boards/deep-dog/baseline-logs/deep-dog-v2.3.0-9ce5885-boot-20260814.log`

| 阶段 | internal free | largest block | internal min | 备注 |
|------|---------------|---------------|--------------|------|
| 小智激活完成 | ~69 KB | — | ~56 KB | OTA/MQTT TLS 刚结束 |
| `post_activation` | ~47 KB | ~23 KB | — | MQTT + HTTP + WS MCP 已起 |
| `face_ai internal ready` | ~32 KB | ~13 KB | — | persist/facedb worker 已建，模型未加载 |
| **`face_ready`** | **~13 KB** | **~3.5 KB** | **~231 B** | 检测+识别模型加载后 |
| 稳态 idle（~40s） | ~17 KB | — | 仍 ~231 B | `health.warn` → `low_internal_heap` |

对比 **esp-sparkbot v2.2.2**（同 OV2640）：激活后 free ~110KB，idle ~82KB（无人脸栈与模型）。

sdkconfig 相关（`sdkconfig.defaults.esp32s3`）：

| 配置 | deep-dog 当前 | sparkbot 参考 |
|------|---------------|---------------|
| `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL` | **131072 (128KB)** | boot log 显示 **96KB** |
| `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP` | y | — |
| LVGL PSRAM cache | 1 MB | 2 MB |

健康阈值（固件已实现，勿随意改语义）：

- `dog_mem_rpt` / `device/status.health.warn`：`internal.free < 32768` → `low_internal_heap`
- 启动门控：`DEEP_DOG_BOOT_MIN_INTERNAL_FREE=24KB`，`DEEP_DOG_BOOT_MIN_INTERNAL_LARGEST=12KB`（`face_ai_config.h`）

## 3. 架构约束（不可违反）

以下组件 **必须** 使用 internal 栈或 internal 分配，迁移 PSRAM 会 panic 或损坏 Flash：

| 组件 | 原因 | 代码 |
|------|------|------|
| `face_facedb` | FAT enroll/delete 触 Flash cache | `face_facedb.cc` · S08 §3.1 |
| `face_persist` | NVS 读写在 PSRAM 栈不安全 | `face_persist.cc` |
| WiFi 静态 RX / 部分 DMA | 硬件约束 | sdkconfig `ESP_WIFI_STATIC_RX_*` |
| Bluepad32/BTstack（若启用） | HCI DMA + Flash bond | [I02](../../input/I02-source-bluepad32-xbox.md) |

已做得较好的部分（**默认保留**，除非本文 §6 明确要求替换）：

- `DEEP_DOG_BOOT_DEFER_HEAVY_UNTIL_ACTIVATION=1`：OTA 完成后再启 Face/MQTT/HTTP
- `dog_face_ai` / `dog_immich` / `vision_hub` 大栈优先 **PSRAM**（`DeepDogFaceTaskCreate` / `xTaskCreateWithCaps`）
- WiFi/LWIP 倾向 PSRAM（`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP`）
- facedb/NVS 与推理分离（PSRAM 算 feat → internal worker 写盘）
- `dog_mem_rpt` + MCP diagnostics + MQTT `mem.internal|psram|tasks[]`

## 4. Internal 消耗归因

```text
┌─────────────────────────────────────────────────────────────┐
│ 固定 reserve：SPIRAM_MALLOC_RESERVE_INTERNAL 128KB          │
├─────────────────────────────────────────────────────────────┤
│ 必须 internal 任务栈：face_persist(3K) + face_facedb(6K)    │
│ 启动/辅助：dog_post_act(4K) · immich_late(4K) · face_boot(16K)│
├─────────────────────────────────────────────────────────────┤
│ 并发服务：mqtt_task · httpd · WS MCP · AFE/WakeNet           │
├─────────────────────────────────────────────────────────────┤
│ 峰值：HumanFaceDetect + HumanFaceRecognizer 加载 (dl::Model) │
│       → min 可跌至数百字节；largest 碎至 ~3.5KB               │
├─────────────────────────────────────────────────────────────┤
│ 运行时偶发：TLS/MQTT/Immich multipart（有 PSRAM 优先+回退）   │
└─────────────────────────────────────────────────────────────┘
```

典型故障链（baseline 已观测）：

1. face 模型加载 → `largest_internal` ≈ 3.5KB  
2. `immich_late`（4096 字栈）创建失败 → Immich worker 延迟或失败  
3. `device/status` 持续 `low_internal_heap`  
4. 极端情况下 TLS/OTA/新 task 因 **无连续 internal 块** 失败  

## 5. 优化项清单（按优先级）

每项格式：**ID · 标题 · 预期收益 · 改动 · 风险 · 验收**

### 5.1 第一梯队（小改、高收益，建议先做）

#### MEM-01 · 启动错峰（Face → Immich → VisionHub → HTTP 微调）

| 项 | 内容 |
|----|------|
| 预期收益 | 降低 face 模型加载与 camera/httpd 同时抢 internal 的概率；`largest` +5～15KB |
| 改动 | `esp_sparkbot_board.cc` `StartPostActivationServices()`：Face runtime OK 且 `DeepDogMemoryWaitInternalReady(..., 8KB largest, ...)` 后再 `vision_hub->Start()`；HTTP/WS MCP 可保持现状或再 delay 1～2s |
| 风险 | 首帧 RTSP/MJPEG 晚 1～3s |
| 验收 | face_ready 后 `largest_internal ≥ 8192`；VisionHub 启动 log 晚于 `dog_face_rec: recognizer ready` |

#### MEM-02 · 取消 `immich_late` 独立 internal 任务

| 项 | 内容 |
|----|------|
| 预期收益 | 省 4KB 栈；消除 `immich_late task create failed` |
| 改动 | `face_ai_runtime.cc`：在 `dog_face_ai` 内 `vTaskDelay` 后调用现有 `DeepDogImmichInit()` 重试环；或 `ScheduleImmichWorkerAfterBoot()` 仅用 `DeepDogFaceTaskCreate` 且确认 PSRAM 栈可用 |
| 风险 | 低；需确认 `PrepareConfig` 仍在 internal 上下文执行一次 |
| 验收 | 无 `immich_late task create failed`；`dog_immich: immich worker ready` 在 face_ready 后 30s 内出现 |

#### MEM-03 · 栈高水位审计 + trim

| 项 | 内容 |
|----|------|
| 预期收益 | 每任务可省 0.5～4KB（视 hwm） |
| 改动 | 启用/确认 `CONFIG_FREERTOS_USE_TRACE_FACILITY`；用 MCP diagnostics 读 `tasks[].stack_hwm`，对照 `memory_report.cc` `LookupStackHint` 调整：`face_facedb` 6144→5120、`face_persist` 3072→2560、`dog_post_act` 4096→3072（仅当 hwm 余量 >25%） |
| 风险 | 栈过小 → 运行时 overflow；**必须**改一项测一项 |
| 验收 | 压测（face+RTSP+Immich）无 stack overflow；hwm 仍 >512 字节余量 |

#### MEM-04 · `SPIRAM_MALLOC_RESERVE_INTERNAL` 128K → 96K 试验

| 项 | 内容 |
|----|------|
| 预期收益 | general internal 池 +~32KB |
| 改动 | `sdkconfig.defaults.esp32s3`（及 handle_bt overlay 若共用） |
| 风险 | WiFi/camera DMA 极端并发 alloc fail；需 RTSP+人脸+Immich 压测 |
| 验收 | 24h 或 `scripts/test_deep_dog_face_cycle.sh` 全 PASS；无 WiFi disconnect / cam alloc fail |

---

### 5.2 第二梯队（架构级，需改 SWRS 验收项）

#### MEM-05 · 合并 `face_persist` + `face_facedb` 为单一 internal worker

| 项 | 内容 |
|----|------|
| 预期收益 | 省 ~3～6KB（一个任务栈 + 队列） |
| 改动 | 新 `face_flash_worker` 串行 NVS + FAT；删除双 task |
| 风险 | 中；clear_db quiesce 逻辑需回归 |
| 验收 | enroll/delete/clear_db/registry/Immich 绑定全 PASS；S08 facedb 验收仍过 |

#### MEM-06 · 识别模型延迟加载（lazy `HumanFaceRecognizer`）

| 项 | 内容 |
|----|------|
| 预期收益 | face_ready 峰值 internal free +10～20KB |
| 改动 | boot 仅 `DeepDogFaceDetectInit`；首次 `RecognizeProcess` / enroll 再 `DeepDogFaceRecognizeInit` |
| 风险 | 首次识别延迟 1～2s；detect-only 路径需明确 |
| 验收 | boot `face_ready` 时 free ≥ 25KB；首次 enroll 成功；detect 不受影响 |

#### MEM-07 · `face_boot`（16KB）改 PSRAM 栈

| 项 | 内容 |
|----|------|
| 预期收益 | 动态启 face 时省 ~16KB internal |
| 改动 | `DeepDogFaceAiSetEnabled(true)` 路径用 `DeepDogFaceTaskCreate` 替代 `xTaskCreate` |
| 风险 | 模型加载在 PSRAM 栈上是否 100% 安全需实机验证（与 facedb 不同，主要是 heap） |
| 验收 | MQTT `face/cmd enabled=true` 冷启动成功；无 cache_disabled assert |

#### MEM-08 · internal 紧张时禁止 heap fallback

| 项 | 内容 |
|----|------|
| 预期收益 | 避免碎片化恶化 |
| 改动 | `immich_client.cc` / `face_ai_runtime.cc`：当 `free_internal < 32KB` 时 multipart/JPEG **不** fallback `MALLOC_CAP_INTERNAL`，直接 skip + 日志 |
| 风险 | Immich 上传 transient 失败（可出镜重试，见 S05） |
| 验收 | internal min 不再因 Immich 上传跌破 512B；upload 仍可通过 PSRAM 路径成功 |

---

### 5.3 第三梯队（产品/配置权衡）

#### MEM-09 · Boot 默认 detect-only，recognize/immich 由 MQTT 打开

| 项 | 内容 |
|----|------|
| 预期收益 | 待机 internal 显著升高 |
| 改动 | `DEEP_DOG_FACE_AI_DEFAULT_ENABLED` 或拆分 detect/recog 默认 |
| 风险 | 产品行为变化；需更新 [04-face MQTT](../../mqtt/modules/04-face.md) |
| 验收 | 默认 boot 无 `recognizer ready`；`face/cmd` 可启全功能 |

#### MEM-10 · IMU / 非关键模块延后到 `face_ready` 之后

| 项 | 内容 |
|----|------|
| 预期收益 | post_activation 前省少量 internal + I2C 栈 |
| 改动 | `esp_sparkbot_board.cc` WiFi 回调中 IMU init 移入 `StartPostActivationServices` 末尾 |
| 风险 | 低；IMU MQTT 晚几秒 |
| 验收 | IMU status 仍在 30s 内发布；face 启动门槛不变 |

#### MEM-11 · LVGL cache 1MB → 512KB（可选）

| 项 | 内容 |
|----|------|
| 预期收益 | PSRAM 为主，internal 间接压力略降 |
| 改动 | 板级 Display 配置 |
| 风险 | 复杂 UI 动画卡顿 |
| 验收 | 触摸/表情页目视 OK |

---

### 5.4 不建议 / 禁止

| 项 | 原因 |
|----|------|
| facedb/NVS 栈迁 PSRAM | Flash cache assert（S08 已文档化） |
| 大幅削减 WiFi static RX（已 3） | 易断流 |
| 未测即合并 `dog_face_ai` 与 `vision_hub` | WDT 与阻塞风险（S08 §2） |
| 无 baseline 对比改多项 sdkconfig | 无法归因 |

## 6. 推荐实施顺序（Agent 执行清单）

**原则：一项一改、编译、烧录、采 baseline、对比 §2 表格后再下一项。**

| 步骤 | ID | 说明 |
|------|-----|------|
| 0 | — | 采 baseline：`scripts/capture_deep_dog_baseline.sh`；MCP `self.board.diagnostics` 保存 JSON |
| 1 | MEM-02 | 去 immich_late internal 任务 |
| 2 | MEM-01 | 启动错峰 VisionHub |
| 3 | MEM-03 | diagnostics 驱动栈 trim |
| 4 | MEM-04 | RESERVE 96K A/B（可回滚） |
| 5 | MEM-08 | Immich/JPEG 禁 internal fallback |
| 6 | MEM-05～07 | 按收益选做；每项独立 PR/提交 |
| 7 | MEM-09～11 | 仅当产品同意行为变更 |

## 7. 回归测试矩阵

| 场景 | 命令/操作 | 通过标准 |
|------|-----------|----------|
| 冷启动 | baseline 脚本 120s | 无 panic；`dog_face_ai runtime started`；Immich worker ready |
| 人脸周期 | `scripts/test_deep_dog_face_cycle.sh` | FAILURES: none |
| internal 健康 | MQTT `device/status` | 稳态 `mem.internal.free ≥ 16KB`（目标；当前 ~17KB） |
| largest 块 | diagnostics | 稳态 `largest_free ≥ 8192`（目标；当前 ~3.5KB at face_ready） |
| RTSP+脸 | stream start + 人脸 | 无 WDT；S08 验收 |
| Immich | 出镜 + upload | `immich_asset_id` 可写入；S05 验收 |
| OTA | 手动或 mock | TLS 握手成功（internal 峰值后） |

日志关键字（失败时查）：

- `immich_late task create failed` / `immich task create failed`
- `health: low_internal_heap`
- `esp_task_stack_is_sane_cache_disabled`
- `Guru Meditation` / `stack overflow`

## 8. 观测与文档同步

| 动作 | 路径 |
|------|------|
| 更新 baseline | `main/boards/deep-dog/baseline-logs/deep-dog-v*-boot-*.log` |
| 栈域说明 | `mqtt/memory_report.cc` `LookupStackHint` |
| 健康阈值 | `mqtt/modules/01-device.md` · `device_mqtt.cc` |
| RTSP 并发 | [S08](./S08-rtsp-face-wdt-mitigation.md) |
| 实施完成后 | 本文「基线状态」改为「已落地 MEM-xx」；ROADMAP 状态更新 |

## 9. 与 S06 高分辨率的交叉影响

[S06 §9](./S06-higher-resolution.md) 若启用 640×480 采集：

- JPEG crop / 推理临时 buffer 增大（主要在 PSRAM）
- internal 峰值更高；**必须先完成 MEM-01～04**，再评估 S06
- Immich 240×240 `people=[]` 问题与 internal 无关，但 upload 频率上升会加剧 §5.1 MEM-08

## 10. 不包含

- PSRAM 优化（当前非瓶颈）
- 换芯片 / 16MB PSRAM 硬件方案
- DeepDiary 云端侧内存
- 人脸算法精度改动

---

**Agent 开工前自检**

- [ ] 已读本文 + [S08](./S08-rtsp-face-wdt-mitigation.md) + baseline log  
- [ ] 本次只做 §6 中 **一个** MEM-ID  
- [ ] 改完更新本文 §2 基线表或 §5 对应项状态  
- [ ] 未扩大 ROADMAP 未列范围  
