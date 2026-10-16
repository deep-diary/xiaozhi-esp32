# V-S09 · Internal SRAM 优化评估与实施指南

| 项 | 内容 |
|----|------|
| 路线图 ID | **V-S09** |
| 优先级 | P1（稳定性 / 可扩展性；非阻塞当前功能交付） |
| 依赖 | [S04](./S04-local-face-numeric-id.md) · [S05](./S05-immich-real-name.md) · [S08](./S08-rtsp-face-wdt-mitigation.md) · [01-device MQTT](../../mqtt/modules/01-device.md) · [N01 SNTP](../../net/N01-sntp-clock-sync.md) |
| 代码落点 | `face_ai/*` · `esp_sparkbot_board.cc` · `mqtt/memory_report.*` · `sdkconfig.defaults.esp32s3` · `vision/vision_frame_hub.cc` · `http-server/*` |
| 基线状态 | **MEM-01 已落地**（带实测偏差修正）；**MEM-02 复核：已满足**；**MEM-04 已实测证伪**（勿按原文实施）；**MEM-03 / MEM-08 已落地（2026-10-16，板级任务栈 −2KB + internal 回退门控）**；MEM-05/06 待评估（见 §5 状态列） |
| 观测工具 | MCP `self.board.diagnostics` · MQTT `status/device` · `scripts/capture_deep_dog_baseline.sh` |

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

### 2.1 复测基线（2026-10-16，F-12 落地 + MEM-01 落地后）

| 阶段 | internal free | largest block | internal min | 备注 |
|------|---------------|---------------|--------------|------|
| 启动（httpd 之前，~13.4s） | **~10.1 KB** | **~3.6 KB** | 163 B | 实测 `dog_ws_mcp` retry log：`free_int=10135 largest_int=3584` |
| WS-MCP + 数据点 REST 起动后（~14.3s） | **~5.1 KB** | **~1.8 KB** | — | **httpd 本身吃掉约 5KB internal**（4096 栈 + TCB/会话） |
| VisionHub 起动后（~30s） | ~4.6 KB | ~1.8 KB | — | MEM-01 门控后启动；起动本身仍成功（相机/JPEG 缓冲走 PSRAM） |
| 稳态 idel（~42s） | **~8.7 KB** | — | 163 B | `SystemInfo: sram(internal): free=8659`；PSRAM free ~7.3MB |
| 稳态（2026-10-16 复测，MEM-01 后 / MEM-03·08 前，face off） | **~13.3 KB** | **~5.6 KB** | **123 B** | 实测 `mem.internal: free=13303 largest_free=5632 total=404035`（WS MCP `self.board.diagnostics`，MQTT/WS-MCP/VisionHub 已起，>60s）；`min=123B` 说明**开机峰值**仍会把 internal 打到百字节级（§4 故障链），与 face 开关无关 |
| 稳态（2026-10-16 复测，MEM-03·08 后，face off） | 10.6–12.6 KB | 4.0 KB | **1687 B** | 同法采样：`free=10615~11503 largest_free=4096`；`cfg_sync` hwm 4960/7168、`httpd` hwm 932/3072（无 overflow）。**注**：`free`/`largest` 随机启动、WS 会话数波动（同一次开机两次采样差 ~0.9KB），跨 boot 差值不可直接当作收益；MEM-03 的收益是**确定性的栈分配 −2KB**。本次 `min` 从 123B 升到 1687B（开机峰值），但样本量不足，勿据此下结论 |


> 关键结论：**WS-MCP 的 httpd 常驻约 5KB internal**；其「起不来」时（见 F-13）反而显示 free≈10KB ——
> 即 WS-MCP 可用性与 internal 余量互为代价。要同时满足 face / VisionHub / WS-MCP，必须靠 §5 的净优化项。

对比 **esp-sparkbot v2.2.2**（同 OV2640）：激活后 free ~110KB，idle ~82KB（无人脸栈与模型）。

sdkconfig 相关（`sdkconfig.defaults.esp32s3`）：

| 配置 | deep-dog 当前 | sparkbot 参考 |
|------|---------------|---------------|
| `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL` | **131072 (128KB)** | boot log 显示 **96KB** |
| `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP` | y | — |
| LVGL PSRAM cache | 1 MB | 2 MB |

健康阈值（固件已实现，勿随意改语义）：

- `dog_mem_rpt` / `status/device.health.warn`：`internal.free < 32768` → `low_internal_heap`
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
3. `status/device` 持续 `low_internal_heap`  
4. 极端情况下 TLS/OTA/新 task 因 **无连续 internal 块** 失败  

## 5. 优化项清单（按优先级）

每项格式：**ID · 标题 · 预期收益 · 改动 · 风险 · 验收**

### 5.1 第一梯队（小改、高收益，建议先做）

#### MEM-01 · 启动错峰（Face → Immich → VisionHub → HTTP 微调）

**状态：✅ 已落地（2026-10-16，带实测偏差修正）**

| 项 | 内容 |
|----|------|
| 预期收益 | 降低 face 模型加载与 camera/httpd 同时抢 internal 的概率；`largest` +5～15KB |
| 改动（实际） | `esp_sparkbot_board.cc` 两处 VisionHub 启动前加门控：`DeepDogMemoryWaitInternalReady("vision_hub", DEEP_DOG_VISION_HUB_MIN_INTERNAL_FREE(4KB), DEEP_DOG_VISION_HUB_MIN_INTERNAL_LARGEST(2KB), DEEP_DOG_VISION_HUB_MEMORY_WAIT_MS(3s))`；常量在 `face_ai_config.h`。另：**WS-MCP httpd 起动改为「等内存达标 + 多次重试」**（见 F-13），修掉「开机 largest<4KB → 本会话无 WS-MCP/数据点 REST」。 |
| 实测偏差（重要） | 原文验收「face_ready 后 `largest_internal ≥ 8192`」**当前剖面不可达**：实测 VisionHub 起动时 `largest_int=1792B`，且**启动仍成功**（相机/JPEG 缓冲在 PSRAM）。按 8KB/15s 原文实施会变成「纯 15s 启动延迟」。故改为**短超时 + 可达阈值**的让路式门控。 |
| 风险 | 首帧 RTSP/MJPEG 晚 ~3s（原为 15s） |
| 验收（修正） | `largest_internal` 不低于门控阈值（4KB free / 2KB largest）即启动；VisionHub 启动不再因内存阻塞 15s |

#### MEM-02 · 取消 `immich_late` 独立 internal 任务

**状态：✅ 复核结论——已满足（无需改动）**

| 项 | 内容 |
|----|------|
| 复核证据 | ① `ScheduleImmichWorkerAfterBoot()` 已用 `DeepDogFaceTaskCreate`（`face_task_util.h`：`xTaskCreateWithCaps(MALLOC_CAP_SPIRAM)` 优先，失败才回退 internal）→ **不占用 4KB internal**；② 原文要求的「`PrepareConfig` 仍在 internal 上下文执行一次」已满足：`DeepDogImmichPrepareConfig()` 在 `face_ai_runtime.cc:531`（`face_boot` 任务，**internal 栈**）先执行，`immich_late` 内的再次调用因 `s_config_ready` 短路为 no-op —— 这一点很关键：`DeepDogImmichApplyDefaultsIfEmpty()` 会读写 NVS（`LoadConfigFromNvs`/`SaveConfigToNvs`），若首次发生在 PSRAM 栈上会触发 `esp_task_stack_is_sane_cache_disabled` 断言（同 F-10）。 |
| 残留风险 | 若将来有人把 `DeepDogImmichPrepareConfig()` 挪到 PSRAM 栈上下文，会立刻复现 F-10 类崩溃；已在 `immich_client.cc` 保留 `init skipped: PrepareConfig not done (internal stack required)` 告警。 |
| 验收 | 无 `immich_late task create failed`；`dog_immich: immich worker ready` 在 face_ready 后 30s 内出现 |

#### MEM-03 · 栈高水位审计 + trim

**状态：✅ 已落地（2026-10-16，按实测稳态 hwm 裁剪板级任务）**

实测方法：设备烧录后跑 `scripts/deep_dog/deep_dog_device_audit.py --device-ip <IP> --via none`（走 WS MCP `self.board.diagnostics`），
取**稳态**（MQTT 已连、WS-MCP/VisionHub 已起，>60s）的 `tasks[].stack_hwm`。
裁剪规则：本文 §5 —— **仅当 hwm 余量 >25%**；裁剪后仍须留 >512B（本次实际留 >1KB）。

| 项 | 内容 |
|----|------|
| 改动① | `config_center/config_manager.cc` `kTaskStack` **8192 → 7168**（`cfg_sync`：hwm=5992，余量 26.9%，裁后余 1176B） |
| 改动② | `ws-mcp/deep_dog_ws_mcp_server.cc` `config.stack_size` **4096 → 3072**（`httpd`：hwm=1964，余量 52%，裁后余 1108B；**顺带降低启动所需连续 internal**，缓解 F-13 的 `largest_int=3584 < 4096`） |
| 收益 | 常驻 internal **−2KB**（两项均为常驻任务栈） |
| 实测不裁（余量不足 25%） | `mqtt_reconn` 6144/hwm=5400（余 12%）、`touch_btn_task` 4096/hwm=3120（余 23.8%） |
| 无数据不裁 | `face_persist` / `face_facedb` / `face_boot` / `dog_post_act` —— 当前 `DEEP_DOG_FACE_AI_DEFAULT_ENABLED=0`，face 任务不常驻，`dog_post_act` 为瞬时任务，均取不到稳态 hwm；**不得凭推测裁剪** |
| 非板级候选（需另行决策） | 实测余量 >25% 但不在 `main/boards/deep-dog/` 内、或为项目级 sdkconfig：`opus_codec` 24576/hwm=16736（**−4KB 潜力**，`main/audio/audio_service.cc`）、`audio_input` 8192/hwm=4616（−2KB，同文件）、`mqtt_task`(esp-mqtt) 6144/hwm=4484（`CONFIG_MQTT_TASK_STACK_SIZE`）、`esp_timer` 8192/hwm=4960（`CONFIG_ESP_TIMER_TASK_STACK_SIZE`，⚠ 承载 IMU 等回调，改前须压测）。按板级隔离规则，这些属公共/框架路径，仅在上表收益耗尽后再评估 |
| 风险 | 栈过小 → 运行时 overflow；**必须**改一项测一项 |
| 验收 | 压测（face+RTSP+Immich）无 stack overflow；hwm 仍 >512 字节余量 |

#### MEM-04 · `SPIRAM_MALLOC_RESERVE_INTERNAL` 128K → 96K 试验

**状态：❌ 已实测证伪 —— 勿按原文实施**

| 项 | 内容 |
|----|------|
| 实测（2026-10） | 128K→96K 后 internal **total 反而从 403955 降到 371187（少 32KB）**，且板级 MQTT 无法启动 → **已回滚**（详见 FINDINGS 第 7 轮）。 |
| 结论 | 该项在当前 SDK 配置下不成立；如需回收 internal，应改走 MEM-03（栈 trim）+ MEM-05/06 + 常驻对象迁 PSRAM。 |

| 项 | 内容 |
|----|------|
| 预期收益 | general internal 池 +~32KB |
| 改动 | `sdkconfig.defaults.esp32s3`（及 handle_bt overlay 若共用） |
| 风险 | WiFi/camera DMA 极端并发 alloc fail；需 RTSP+人脸+Immich 压测 |
| 验收 | 24h 或 `scripts/test_deep_dog_face_cycle.sh` 全 PASS；无 WiFi disconnect / cam alloc fail |

---

### 5.2 第二梯队（架构级，需改 SWRS 验收项）

#### MEM-05 · 合并 `face_persist` + `face_facedb` 为单一 internal worker

**状态：⏸ 暂不落地（前置条件未满足）**

| 项 | 内容 |
|----|------|
| 阻塞原因 | ① 需 face 运行时**可跑**才能回归 `enroll/delete/clear_db/registry/Immich 绑定`（当前 `face_boot` 起不来，见 F-10）；② `clear_db` 会**删除已入库人脸**（`TC0500`/face 库 CRUD 需用户授权）；③ 属 `quiesce` 逻辑重构，回归面大、收益 3～6KB。 |
| 建议顺序 | 先按 F-10「拆 internal-flash 小任务 + PSRAM 模型任务」让 face 起得来 → 再评估本项。 |

| 项 | 内容 |
|----|------|
| 预期收益 | 省 ~3～6KB（一个任务栈 + 队列） |
| 改动 | 新 `face_flash_worker` 串行 NVS + FAT；删除双 task |
| 风险 | 中；clear_db quiesce 逻辑需回归 |
| 验收 | enroll/delete/clear_db/registry/Immich 绑定全 PASS；S08 facedb 验收仍过 |

#### MEM-06 · 识别模型延迟加载（lazy `HumanFaceRecognizer`）

**状态：⚠️ 部分已具备；本轮未落地（原因见下）**

| 项 | 内容 |
|----|------|
| 已具备 | 识别入口**已经是懒加载**：`face_recognize.cc::DeepDogFaceRecognizeProcess()` 首行即 `if (!DeepDogFaceRecognizeReady() && !DeepDogFaceRecognizeInit()) return;`。 |
| 未改动的唯一 eager 点 | `face_ai_runtime.cc:561` 的 `DeepDogFaceRecognizeInit()`（RuntimeStart 内，internal 的 `face_boot` 栈）。 |
| 为何不落地 | ① 若删掉该 eager init，首次初始化会发生在 `dog_face_ai` 任务（**PSRAM 栈**）上——模型加载在 PSRAM 栈的安全性**未验证**（F-10 同类风险，不可凭推测改动）；② 当前 face 根本起不来（见下），**无法端到端验证**该改动。 |
| 关键结论 | **MEM-06 不是「face 起不来」的主因**。实测 `face_boot` 任务需要 **16KB 连续 internal**，而当前 `largest_int` 仅 1.8～4.3KB → 任务创建即失败（F-10）。正确方向是 F-10 §「正确方向 2」：把 `RuntimeStart` 拆成「internal 小任务做 NVS/FAT（persist/facedb）」+「PSRAM 任务做模型加载」，而不是继续推迟识别模型。 |
| 验收（原文） | boot `face_ready` 时 free ≥ 25KB；首次 enroll 成功；detect 不受影响 |

#### MEM-07 · `face_boot`（16KB）改 PSRAM 栈

| 项 | 内容 |
|----|------|
| 预期收益 | 动态启 face 时省 ~16KB internal |
| 改动 | `DeepDogFaceAiSetEnabled(true)` 路径用 `DeepDogFaceTaskCreate` 替代 `xTaskCreate` |
| 风险 | 模型加载在 PSRAM 栈上是否 100% 安全需实机验证（与 facedb 不同，主要是 heap） |
| 验收 | MQTT `cmd/face enabled=true` 冷启动成功；无 cache_disabled assert |

#### MEM-08 · internal 紧张时禁止 heap fallback

**状态：✅ 已落地（2026-10-16）**

| 项 | 内容 |
|----|------|
| 预期收益 | 避免 internal 碎片化恶化（大缓冲不再切走 internal 连续块） |
| 阈值 | 新增 `DEEP_DOG_FACE_INTERNAL_FALLBACK_MIN_FREE`（`face_ai_config.h`，默认 **32KB**，与 `memory_report.cc` 的 `low_internal_heap` 阈值一致） |
| 改动（实际） | 新增共用判定 `DeepDogInternalFallbackAllowed()`（`face_ai/face_task_util.h`）。以下 5 处 PSRAM 失败后的 `MALLOC_CAP_INTERNAL` 回退改为**先过阈值判定**，不满足则直接跳过并打日志（不分配）：<br>① `face_ai_runtime.cc` `CropFaceToJpeg` crop 缓冲；② 同函数裁剪脸 JPEG 副本；③ `FullFrameToJpeg` 整帧 JPEG 副本；④ `immich_client.cc` `HttpEvent` 响应缓冲 `realloc`；⑤ `immich_client.cc` multipart 请求体。 |
| 不改 | 人脸检测送帧副本（`face_ai_runtime.cc` `SubmitFrame`，非 multipart/JPEG，且 PSRAM 充足时不会走回退）；`dog_face_ai` / `vision_hub` 等 PSRAM 栈任务创建（`DeepDogFaceTaskCreate` 已优先 PSRAM）。 |
| 风险 | Immich 上传/取名单在 internal 紧张时会更早失败（可出镜重试，见 S05；`DEEP_DOG_FACE_IMMICH_BACKOFF_S` 已退避） |
| 验收 | internal 低水位（free<32KB）时日志出现 `internal fallback skipped`（而非把 internal 吃到 <512B）；PSRAM 路径不受影响，`immich jpeg ready` / `upload ok` 在 PSRAM 充足时仍出现 |


---

### 5.3 第三梯队（产品/配置权衡）

#### MEM-09 · Boot 默认 detect-only，recognize/immich 由 MQTT 打开

| 项 | 内容 |
|----|------|
| 预期收益 | 待机 internal 显著升高 |
| 改动 | `DEEP_DOG_FACE_AI_DEFAULT_ENABLED` 或拆分 detect/recog 默认 |
| 风险 | 产品行为变化；需更新 [04-face MQTT](../../mqtt/modules/04-face.md) |
| 验收 | 默认 boot 无 `recognizer ready`；`cmd/face` 可启全功能 |

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
| internal 健康 | MQTT `status/device` | 稳态 `mem.internal.free ≥ 16KB`（目标；当前 ~17KB） |
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
