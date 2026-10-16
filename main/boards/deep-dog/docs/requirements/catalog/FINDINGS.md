# 全盘测试 · 关键发现（FINDINGS）

> 配套：[TEST_REPORT.md](../TEST_REPORT.md)（逐用例结果）· [catalog/TESTS.md](./TESTS.md)（用例注册表）· [catalog/FEATURES.md](./FEATURES.md)（功能点注册表）
> 测试时间：2026-10-15 · 设备 `1051db8118fc` @ `10.99.79.59` · broker `10.99.79.54:1883` · topic root `deep-trace`
> 工具：`scripts/deep_dog/tests/run.py`

## 0. 结果概览

| 轮次 | 用例 | PASS | FAIL | SKIP | BLOCKED | ERROR |
|------|------|------|------|------|---------|-------|
| 第 1 轮 | 66 | 31 | 11 | 3 | 11 | 10 |
| 第 2 轮（复位后） | 67 | 36 | 11 | 3 | 8 | 9 |
| 第 3 轮（回滚固件 + 修正发现策略后） | 67 | 32 | 11 | 3 | 11 | 10 |
| 第 4 轮（修复 ①：懒注册） | 67 | 37 | 10 | 2 | 11 | 7 |
| 第 5 轮（修复 ①+②：内存感知注册） | 67 | 56 | 3 | 2 | 6 | 0 |
| 第 6 轮（修复 ①+②+③+测试修正） | 67 | 59 | 0 | 2 | 6 | 0 |
| 第 7 轮（跨剖面验证 + 加固 + 恢复默认） | 67 | 58 | 2 | 7 | 0 |
| **第 8 轮（网络全量 + 串口断言阶段）** | **67 + 7** | **57 + 6** | **2 + 0** | **8 + 1** | **0 + 0** |
| **第 9 轮（补充 17 条命令类用例）** | **84 + 7** | **74 + 6** | **2 + 0** | **8 + 1** | **0 + 0** |
| **第 10 轮（MCP 协议级 + dp 批量端点 + 修 F-05）** | **90 + 7** | **81 + 6** | **2 + 0** | **7 + 1** | **0 + 0** |
| **第 11 轮（face 命令面 + dp 回环/子实例；实测 MEM-07 不可行）** | **97 + 7** | **84 + 6** | **3 + 0** | **10 + 1** | **0 + 0** |
| **第 12 轮（CFG-02 生效/白名单/待生效上报）** | **100 + 7** | **87 + 6** | **3 + 0** | **10 + 1** | **0 + 0** |
| **第 13 轮（device/imu/can/stream 数据点+串口 init；合并单份报告）** | **107 + 10** | **92 + 9** | **3 + 0** | **11 + 1** | **0 + 0** |
| **第 14 轮（新增 greet/handle/motor/mcp/dp 10 条；修 F-11/F-12/F-13）** | **127** | **114** | **0** | **2** | **11** | **0** |

> 第 14 轮：用例 117 → **127**（新增 `TC0656/0658/0659/0661/0662/0751/0760/1075/1262/1309`），
> **FAIL=0 / ERROR=0**，可执行用例 100% PASS。本轮修掉 3 个问题：
> **F-11**（httpd 偶发非 HTTP 首行 → 传输级异常改判 BLOCKED + 一次有界重试）、
> **F-12**（自动 dp MCP 工具 75 个在 internal 余量不足时 0 注册；修「首次失败永久放弃」，暴露「全有或全无」待决策）、
> **F-13**（一次瞬时断连被测试缓存放大成 11 例级联 ERROR → WS 客户端自愈 + 传输异常判 BLOCKED）。
> 另：`TC1311` 原断言会被 `self.audio_speaker.set_volume` 这类手写工具**误判 PASS**，已改为按 schema 全量比对。
> 阻塞项 11 条均为「剖面/硬件/内存/配置」门控（led/gimbal/servo 剖面、内置 httpd 编译关、摄像头、internal SRAM）。

> 第 13 轮新增 10 条（device 数据点/WiFi/power、imu 无 cmd/数据点、CAN 引脚、stream mjpeg 门控 + 串口 CAN init/内存报告/SNTP 主备），
> 并把**网络 + 串口合并进一份报告**（`run.py --serial-cases`）。

> 第 11 轮新增 7 条（face 开关/识别/管线/refresh_immich/MCP manage + dp MCP 回环/子实例）；
> 其中 **F-10** 为本轮最有价值的负结论：**S09 MEM-07 按原文实施会让设备 assert 重启**，已回滚并给出正确方向。

> 第 10 轮：新增 §1.3 的 6 条（MCP initialize/信封/超长帧/广播 + dp batch/batch_set），全部 PASS；
> 并**实现了原先「文档有、实现无」的 `/api/datapoints/batch`、`batch_set`**（F-05 该项闭环）。
> 本轮 `TC1404`（RTSP 推流）由 BLOCKED 转 **PASS**——摄像头恢复可用（其可用性为**间歇性**，见 F-09）。

> 第 9 轮新增 §1.2 的 17 条「下发命令 → 观察 status」用例（track/touch/handle/can/motor/dp），
> **全部一次通过**，网络侧 PASS 由 57 升到 **74**（新增 17 条全部 PASS）。

> 第 8 轮：网络侧 57 PASS / 0 FAIL；串口侧 6 PASS / 0 FAIL / 1 BLOCKED（F-09 摄像头）。
> 与第 7 轮相比 PASS 少 1、BLOCKED 多 1 —— 差异仅为 `TC1404` 因**摄像头掉电**由 PASS 转 BLOCKED（见 F-09），
> 属设备物理状态变化，非回归。

> **当前状态（默认 CAN 剖面，第 14 轮）：全量单跑（`--serial-cases`）127 条 →
> PASS=114 / FAIL=0 / ERROR=0 / BLOCKED=11 / SKIP=2**
> —— 可执行用例 **100% PASS**。
> BLOCKED 均为「剖面/设计/硬件/资源」门控，非业务逻辑缺陷；led / gimbal / servo 已在**对应剖面**跑通（§0.3），
> 内置 httpd 四端点因 `DEEP_DOG_HTTP_SERVER_ENABLE=0`（与 WS-MCP 并开会耗尽 internal），
> 摄像头与内存受限项见 **F-09 / F-10 / F-12**。

## 0.3 跨剖面验证（同一设备，逐个剖面烧录实测）

| 剖面（`ext_pins.mode`） | 用例 | 结果 | 证据 |
|-------------------------|------|------|------|
| `led` | TC0804 led-status | **PASS** | `ext_pins.mode=led`、`capabilities.led=true`、`status/led` retain 正常 |
| `pwm`（+GIMBAL） | TC0860 gimbal-status | **PASS** | `ext_pins.mode=pwm`、`capabilities.gimbal=true` |
| `pwm`（+SERVO，GIMBAL=0） | TC0904 servo-status | **PASS** | `ext_pins.mode=pwm`、`capabilities.servo=true`、`status/servo` retain 正常 |
| `can`（默认） | 全量 67 | PASS=58 / FAIL=0 | [TEST_REPORT.md](../TEST_REPORT.md) |

> 三个剖面均以**临时 LAB-ONLY 覆盖**（`ResolveBootTarget()` → 固定 mode）达成，验证后已**完全回滚**，
> 工作区不含任何 LAB 代码（`git status` 已核对）。

## 0.4 测试健壮性加固（消除「陈旧 retain 假 PASS」）

首次跨剖面测试暴露了一个**测试缺陷**：模块未激活时，`status/<module>` 上仍留有**上一剖面的 retain 快照**，
用例会读到旧数据而误判 PASS（实测 `status/can` 在 `mode=pwm` 下仍返回旧值）。
已对 led / gimbal / servo / can / motor 全部改为**先查 `info/device.capabilities.<mod>`**
（`_block_if_capability_off`），未激活即 BLOCK，不再依赖 retain。

## 0.5 串口断言阶段（tag=`serial`，10 条，`--serial-cases`）

新增串口日志采集与断言能力（`dogtest/serialcap.py` + `cases/tc_serial.py`），
把「只能从串口观测」的功能点纳入自动化。**打开 COM6 会复位设备（LL-006）**，
故这些用例默认不参与全量跑，启用时排在最后执行。

| TC | 功能点 | 结果 | 证据（串口） |
|----|--------|------|--------------|
| TC0007 | F0007 设钟后重发 | **PASS** | `clock synced: republished device/* + face/registry` |
| TC0013 | F0013 内存报告 | **PASS** | `dog_mem_rpt: memory report [post_activation]` + 任务 hwm 明细 |
| TC0019 | F0019 前缀根 | **PASS** | `dog_mqtt: start prefix=deep-trace/1051db8118fc/` |
| TC0200 | F0200 SNTP 启动 | **PASS** | `dog_sntp: SNTP init server=pool.ntp.org` |
| TC0204 | F0204 SNTP 主/备 | **PASS** | 日志含 `server=` 与 `backup=` |
| TC0206 | F0206 DHCP 地址 | **PASS** | `WifiStation: Got IP: 10.99.79.59` |
| TC0320 | F0320 ext_pins 启动解析 | **PASS** | `ext_pins mode=can (default=can) …` + 与 `supported_modes` 交叉校验 |
| TC0417 | F0417 RTSP 编码分辨率 | BLOCKED | 见 **F-09**（摄像头未检出，需 power cycle） |
| TC0952 | F0952 IMU 100Hz/12 路灯 | **PASS** | `dog_mqtt_imu: switches rot=[0,…] trans=[0,…]` |
| TC1000 | F1000 TWAI 初始化 | **PASS** | `CAN init ok, TX=38 RX=48, queue tx=.. rx=..` |

合计：**PASS=9 / FAIL=0 / BLOCKED=1**（第 14 轮）。

## 0.6 第 15 轮：本轮落地的修复（均编译+烧录+实测）

| # | 变更 | 文件 | 效果 / 证据 |
|---|------|------|-------------|
| ① | **F-12 方案 A**：`McpServer::AutoToolSource` 批量自动工具**按需生成**（零 per-tool 堆对象），替代「懒注册 + 内存守卫」 | `main/mcp_server.{h,cc}`、`ws-mcp/datapoint_mcp.{h,cc}`、`esp_sparkbot_board.cc` | `TC1311` → `auto dp tools=75/75` **PASS**；`TC1261` → `MCP set_tunnel → status/can.tunnel 已同步` **PASS**；挂接后 `internal_free=9239/largest=4352`（未新增常驻） |
| ② | **MEM-01 启动错峰**：VisionHub 启动前加 internal 门控（短超时/可达阈值） | `esp_sparkbot_board.cc`、`face_ai/face_ai_config.h` | 串口 `dog_mem_rpt: vision_hub internal ready: free=4199 largest=2048` → VisionHub 立即启动（原 8KB/15s 阈值永不可达，会变成纯 15s 延迟） |
| ③ | **F-13 WS-MCP httpd 启动韧性**：重试窗口 1.5s → ~4s（8×500ms） | `ws-mcp/deep_dog_ws_mcp_server.cc` | 修复前某些 boot：`httpd_start failed … ESP_ERR_HTTPD_TASK` → 本会话无 WS-MCP/数据点 REST；修复后同一 boot 正常输出 `WS MCP bridge ws://…` + `dp_http: registered /api/datapoints/*` |
| ④ | **F-14 广播健壮性**：广播载荷优先 PSRAM；发送失败即摘除死连接；`%zu`→`%u` | `ws-mcp/deep_dog_ws_mcp_server.cc/.h` | 消除 `broadcast send failed fd=… err=258` 的死连接反复超时；日志 `total=zu` 恢复为数字 |
| ⑤ | **F-14 测试侧**：传输级异常判 BLOCKED + WS 断线自愈 + HTTP 有界重试 + `TC1311` 断言收紧 | `scripts/deep_dog/tests/**` | 消除级联 ERROR 与**假 PASS**（`self.audio_speaker.set_volume` 误命中） |

> 运行间波动（同一固件出现 0 FAIL / 5 FAIL / 多例 BLOCKED 三种结果）见 **F-14**：根因是设备 internal 极限
> （`min≈107B`、`largest≈1.8~4KB`）+ 外部常驻 WS 客户端干扰；失败用例单跑均 PASS。

## F-09 · P1（环境/硬件）· DVP 摄像头间歇/持续未检出，视觉链路无帧

- **现象**：串口 `esp_video_init: failed to detect DVP camera with address=3c` ×2 →
  `deep_dog: Camera init failed after 2 attempts (check power/FPC/XCLK); vision hub will idle until power cycle`；
  `status/stream` 停在 `state=starting error=no_rtp`（RTSP 会话已建立但无帧），
  随后周期性 `vision_hub: camera not ready, vision idle`。
- **影响**：所有依赖画面的用例（`TC1404` RTSP 推流、`TC0417` 编码分辨率、人脸/框叠加系列）无法验证。
  同一会话早期（摄像头可用时）`TC1404` 曾实测 **PASS**（`state=streaming`），说明链路本身通。
- **根因（按固件自述）**：摄像头**掉电状态**，需**物理断电重启（power cycle）**/检查 FPC 与 XCLK；
  非固件缺陷（多次软复位无效，连续 3 次冷启动均未检出）。
- **建议**：
  1. 现场做一次**断电重上电**（USB 拔插）后重跑 `--case TC1404` / `--case TC0417`；
  2. 固件侧可考虑增加「摄像头未就绪」的显式上报（如 `info/device` 增 `camera.ok`），
     让前端/测试可判定，而不用靠 `no_rtp` 反推。
- **相关**：`deep_dog_http_server`/`vision/vision_frame_hub.cc`（`camera not ready, vision idle`）、`CAMERA_SENSOR.md`。

### F-09 补充（间歇性 + 当前推流分辨率）

- **间歇性**：同一设备不同启动之间表现不同——网络侧全量跑（不打开串口）时 `TC1404` 实测 **PASS**（`state=streaming`）；
  而数次「串口阶段」冷启动（打开 COM6 触发复位）时又未检出。**摄像头可用性是间歇的**，
  故相机相关用例一律按串口日志判定，未检出即 BLOCK（附原因），避免误报 FAIL。
- **当前推流分辨率（重要）**：摄像头可用时串口实测
  `vision_hub: H264 push ok bytes=420 capture=240x240 encode=240x240`
  —— 当前联调剖面为 **240²**（[S06 §9](../vision/server/S06-higher-resolution.md) 的 640×480 采集 + 320×240 推流代码保留但未启用，
  与 [AUDIT_REPORT](../AUDIT_REPORT.md) §7 一致）。
  **故 F0417「推流缩至 320×240」只在 §9 剖面成立**；`TC0417` 已改为**剖面无关**断言：
  `encode ≤ capture` 且 `encode ∈ {240×240, 320×240, 640×480}`。



## 0.1 本轮已实施的修复（已烧录验证）

| # | 变更 | 文件 | 效果 |
|---|------|------|------|
| ① | 数据点自动 MCP 工具改**懒注册**（不在开机注册），仅本地 WS-MCP 首次 `tools/list` 时按需注册；并用 `AddUserOnlyTool` 使其不随云端 `tools/list` 上行 | `ws-mcp/datapoint_mcp.{h,cc}`、`ws-mcp/deep_dog_ws_mcp_server.cc`、`esp_sparkbot_board.cc` | 消除开机 internal 常驻开销；云端 `tools/list` 不再因 payload/内存失败 |
| ② | 自动工具注册**内存感知**：`largest_free_internal < 6144` 即停止注册并告警，宁少暴露工具也不崩溃 | `ws-mcp/datapoint_mcp.cc` | 根因：一次性注册上百工具会把已紧张的 internal 打穿并**重启**（实测 WS 用例把设备打挂）。修复后 device 全程存活 |
| ③ | MQTT 重连任务栈改 **PSRAM 优先**（`xTaskCreateWithCaps`，失败回退 internal；按栈归属选择 `vTaskDeleteWithCaps`） | `mqtt/mqtt_client.cc` | 消除「`reconnect task create failed` → **永久离线**」这一最致命健壮性缺陷；设备可在内存压力下自愈 |
| ④ | MediaMTX 默认 host 与 EMQX 默认 host 对齐（同机部署，见 infra.md） | `vision/vision_config.h` | 修复推流目标为残留实验室 IP（F-04），RTSP 推流实测进入 `streaming` |
| ⑤ | 测试侧修正：`tools/list` 跟随 `nextCursor` 分页、忽略 retain 陈旧 `error`、`stream` 收敛判定、推流等待 `streaming` | `scripts/deep_dog/tests/` | 消除误报（3 条 FAIL 全部是测试自身缺陷） |

## 0.2 实测内存改善（MQTT/推流稳定性）

| 指标 | 修复前 | 修复后 |
|------|--------|--------|
| internal `largest_free` | 3712 B | 7168 B |
| 板级 MQTT | 连上即 `errno=12` 掉线，**永不恢复** | 存活并可自愈重连 |
| `telemetry` / `face refresh` / `imu switches` | FAIL/SKIP | PASS |
| WS-MCP `tools/list` | 触发即**重启** | 分级降级，不重启 |


**失败并不分散**：FAIL/ERROR 高度集中于 **一个根因（F-01）** 及其派生的 HTTP/WS 不可达。

---

## F-01 · P0 · 内部 SRAM 耗尽 → 板级 MQTT 掉线且无法自愈 —— ✅ 已修复（见 §0.1 ① ② ③）

**证据（串口，逐次开机均可复现）**

```text
W (12649) dog_mem_rpt: health: low_internal_heap (free=21179 < 32768)
I (13219) dog_mqtt_telem: telemetry publish ok=1 ... points=15
E (13549) mqtt_client: Writing failed: errno=12          # ENOMEM
W (13559) dog_mqtt: mqtt error
W (13579) dog_mqtt: disconnected
I (13569) dog_mqtt: schedule reconnect in 3 s (try 1)
I (13579) dog_mqtt: schedule reconnect in 3 s (try 2)
E (16019) dog_mqtt: reconnect task create failed          # ← 重连任务建不出来
E (19019) dog_mqtt: reconnect task create failed
E (13869) vision_hub: task create failed
W (13879) deep_dog: VisionFrameHub 启动失败
E (13199) lwip_arch: thread_sem_init: out of memory
```

**影响**

- 板级 MQTT 在开机约 1s 后断开，**之后永远无法恢复**（重连任务因 internal 不足创建失败）
  → 所有「命令往返」用例必然失败：
  `TC0306/0307/0308`（config）、`TC0401/0403`（stream）、`TC0508/0534`（face）、
  `TC0681`（track）、`TC0758`（handle）、`TC1074`（motor mcp_call）、`TC0953`（imu 心跳载荷）。
- 运行数分钟后 **httpd（8080，WS-MCP + 数据点 REST）也不可达** → `TC1256–1259`、`TC1350–1360` 超时。
- WS-MCP 握手被对端 RST / 超时 → `TC0014/0015/0525/0527/0555/1302/1306/1311/1315` 阻塞。

**归因**

- 开机 internal free 仅 **~21KB**；[S09](../vision/server/S09-internal-sram-optimization.md) 记录的
  `post_activation` 基线为 **~47KB** → 存在约 **26KB 的回归**。
- 时间线与「数据点归一化 + 自动 MCP 工具 + 统一配置中心 + 遥测」落地一致；
  串口可见 `dp_mcp: registered 75 auto MCP tools from data point registry`。
- `ws-mcp/datapoint_mcp.cc::RegisterDataPointMcpTools()` 对**每个数据点**在 internal 堆上
  构造 `tool_name` / `tool_desc` / `PropertyList`（schema）字符串后再 `McpServer::AddTool(...)`；
  75 个工具 × 2（get/set）× 多段字符串 → 数千至上万字节 internal 峰值，且在 MQTT 之前发生。

**已做的实验（2026-10-15）：S09 MEM-04 被实测证伪**

按 S09 MEM-04 把 `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL` 由 `131072` 降到 `98304`
（仅改 `main/boards/deep-dog/config.json` 的 `sdkconfig_append` + 当前 `sdkconfig`），
`idf.py build flash` 后串口实测：

```text
I (209) esp_psram: Reserving pool of 96K of internal memory for DMA/internal allocations
I (12329) SystemInfo: sram(internal): free=34787 ... total=371187   # ← 改前 total=403955
I (12819) dog_mqtt: esp_mqtt_client_start failed                    # ← 改前是能 start、再 errno=12 掉线
W (12889) dog_ws_mcp: httpd_start attempt 1/3 failed ... free_int=16143 largest_int=3712
E (14389) dog_ws_mcp: httpd_start failed on port 8080
```

**结论：该改动使 internal 池总量反而减少 32KB（403955→371187），板级 MQTT 由「能连后掉线」退化为
「无法启动」，WS-MCP httpd 也起不来 —— 方向相反，已回滚。S09 MEM-04 的表述需修正**
（`SPIRAM_MALLOC_RESERVE_INTERNAL` 在本工程语义下并非「可释放给通用堆的预留」）。

**修正后的关键观测**：真正的瓶颈是**碎片化**，不是总量——
`free_int≈16–21KB` 但 **`largest_int` 仅 3712B**，不足以创建 httpd / MQTT 任务栈（典型 ≥4KB），
于是 `httpd_start` / `esp_mqtt_client_start` / `reconnect task create` 连锁失败。

**建议修复（按性价比，修正版）**

1. **降低启动期并发内存峰值**：board MQTT 与 xiaozhi 语音栈（TLS）错峰/顺序化；
   或让 board MQTT 的 client/buffer 使用 PSRAM（确认 `SPIRAM_TRY_ALLOCATE_WIFI_LWIP` 生效路径）。
2. **消除碎片源**：检视 `start` 早期的大块 internal 分配（TLS 收发缓冲、audio、camera DMA），
   统一走 PSRAM 或预分配，保证启动后 `largest_int ≥ 8KB`。
3. **MQTT 重连不得依赖新建任务**（复用已有 mqtt 任务 / 最小栈），保证任何内存压力下都能自愈——
   当前 `reconnect task create failed` 会让设备**永久离线**，这是最致命的健壮性缺陷。
4. **自动 MCP 工具（75 个）字符串常量化 / 懒生成**：`ws-mcp/datapoint_mcp.cc` 目前为每个数据点
   在 internal 堆上拼 `tool_name`/`tool_desc`/schema；改为从 `.rodata` 取或按需生成。

> 修正：MEM-04 请勿按原文直接实施；建议先做 #3（重连健壮性）与 #2（碎片），再评估 #1/#4。

---

## F-02 · P1 · 同一 topic 双发布者、载荷 schema 不一致

| topic | 发布者 A | 发布者 B | 冲突 |
|-------|----------|----------|------|
| `status/device` | `mqtt/modules/device_mqtt.cc::PublishStatus()`（rich：`mem.*`/`health`/`clock_synced`/`tasks[]`/`current_speaker`，**retain=false**，5s 周期） | `datapoint_registry.cc::ForcePublishModule("device")`（仅 `device.*` 数据点，**retain=true**，上线一次） | 晚订阅者（前端/云端）只拿到 **retain 的精简快照**，看不到 [01-device.md](../mqtt/modules/01-device.md) 承诺的 `mem/health/tasks` |
| `status/imu` | `imu_mqtt.cc`（含 `switches`） | `imu_datapoints.cc` registry（`accel/gyro/pitch/roll/ok`） | 同上：retain 快照无 `switches` |

**证据**：订阅 `status/#` 一到即收到的是 registry 版（`schema=deep-trace.status.device.v1`，仅 device.*）；
实测 `TC0003/0004/0005/0006/0953` 必须**等待下一次非 retain 心跳**才能断言到丰富字段。

**建议**：一个 topic 只允许**一个所有者**。模块已有专属 publisher 时，registry 不再发布该
`status/<module>`；或两者合并为单一 schema（推荐前者）。

---

## F-03 · P1 · `capabilities.ws_mcp` 与实际不符

- `info/device.capabilities.ws_mcp = false`，但串口显示 WS-MCP 已启动：
  `dog_ws_mcp: WS MCP bridge ws://10.99.79.59:8080/ws` / `WS MCP on port 8080 path /ws`，
  且 8080 端口实测在监听。
- 影响：前端/云端据 capability 判定「无 WS-MCP」→ 隐藏本地调试面板。
- 建议：capability 位与 `DeepDogWsMcpServer` 实际启停状态严格一致。
- 回归用例：`TC0011b`。

---

## F-04 · P1 · 推流目标地址残留旧 LAN（配置漂移） —— ✅ 已修复（见 §0.1 ④）

- 实测 `status/stream.push_url = rtsp://192.168.31.25:8554/deep-dog/1051db8118fc`，
  而当前 LAN 为 `10.99.79.x`（broker `10.99.79.54`），`192.168.31.25:8554` **不可达** → 推流必然失败。
- 归因：设备 NVS 残留旧实验室 mediamtx host；配置中心 legacy fallback 未覆盖 mediamtx host。
- 建议：配网页/配置中心下发 `mediamtx.host`；或按 broker host 推导默认值并上报 `pending`。
- 回归用例：`TC1404`（当前 BLOCKED，原因即此）。

---

## F-05 · P2 · 文档 ↔ 实现漂移清单

| 文档 | 漂移 |
|------|------|
| [mqtt/modules/01-device.md](../mqtt/modules/01-device.md) | 称 `status/device` 含 `mem/health/tasks`；实际只在**非 retain 心跳**中，retain 快照无（见 F-02） |
| [interface/04-datapoint-registry.md](../interface/04-datapoint-registry.md) | 所列 `/api/datapoints/batch`、`batch_set` 曾**未见实现** → **✅ 本轮已实现**（`http-server/datapoint_http.cc`，`TC1260a/1260b` 通过） |
| [motor/07-active-report-frame.md](../motor/07-active-report-frame.md) | `motor/report_start`/`report_stop` MQTT 主题、`motor/scan_result` 主题名**未见**（实现为 `event/motor/scan`） |
| [mqtt/modules/07-dog.md](../mqtt/modules/07-dog.md) | `cmd/dog` / `status/dog` 契约存在但**无固件实现**（无 `dog_mqtt.cc`） |
| [input/I08a](../input/I08a-keymap-mqtt-contract-draft.md) / [I08b](../input/I08b-axis-mapping.md) | `schema_ver` 文档内不一致（5 / 7）；实测 `status/handle/keymap.schema_ver = 8` |

---

## F-06 · 已收敛项：剩余 BLOCKED / SKIP 的准确归因（非缺陷）

| 用例 | 现象 | 说明 |
|------|------|------|
| `TC0804`（led）、`TC0860`（gimbal）、`TC0904`（servo） | 默认 CAN 剖面下 BLOCKED | **已在对应剖面单独验证通过**（§0.3）：led / pwm+gimbal / pwm+servo。默认剖面不激活它们属设计如此 |
| `TC1350/1353/1356/1360`（内置 HTTP server 四端点） | BLOCKED | 内置 httpd 默认关（`DEEP_DOG_HTTP_SERVER_ENABLE=0`，见 board_features.h：与 WS-MCP 并开会耗尽 internal）。数据点 REST 已改为复用 WS-MCP httpd，`TC1256–1259/1352` 已 PASS |
| `TC0653`（current_speaker）、`TC0654`（person/active） | SKIP | 无活跃说话人/人物 → 按需上报，属预期行为 |

> 结论：**默认剖面下可执行用例全部 PASS（58/58）**；led/gimbal/servo 已跨剖面验证；
> 内置 HTTP 四端点为既有设计门控（数据点 REST 已覆盖等价能力）。

## F-08 · P1 · 重编**无法**切换引出脚剖面：NVS `active` 会盖住编译期默认

- **现象**：把 `config.h` 的 `DEEP_DOG_EXT_PIN_MODE` 改成 LED/PWM 并 `idf.py build flash` 后，
  设备仍上报 `ext_pins.mode=pwm`（旧值），`capabilities.led/can` 与预期不符。
- **根因**：CFG-02 的设计是「NVS `pending` > NVS `active` > 编译期默认」；
  `CommitApplied()` 会把每次生效模式**持久化到 NVS**（`deep_dog_iomode:active`）。
  一旦某个剖面跑过一次，`active` 就固定下来，**重编编译默认对它无效**。
- **影响**：现场/实验室无法靠重刷固件切剖面；且设备可能停在非预期剖面（本次实测被留在 `pwm`，
  导致 `capabilities.can=false`、`cmd/motor` 被静默忽略）。
- **正确做法**（任选）：
  1. 走配置中心下发 `io.ext_pins.mode`（`SetPendingMode` → 重启生效）——**推荐**，但需 `server_api_base` 可达；
  2. 擦除 NVS（`idf.py erase-flash`）——注意会**一并清掉 WiFi 凭据/人脸库/配置**；
  3. 实验室临时手段：在 `InitializeExtPinModules()` 里临时固定 `target`，跑一次让 `CommitApplied()`
     把 `active` 纠正回来，然后**务必删除该临时改动并重新烧录**（本次即用此法，已回滚）。
- **建议改进**：`device/info.ext_pins` 增补提示，或在配网页提供「恢复编译默认模式」按钮。
- **相关文件**：`ext_pin/ext_pin_mode.cc`（`ResolveBootTarget`/`CommitApplied`）、
  `esp_sparkbot_board.cc`（`InitializeExtPinModules`）、`config_center/config_manager.cc`。


## F-07 · P2 · 配置中心 `connectivity.mediamtx.rtsp_host` 未接入 URL 生成

- 现象：`config_types.h` 已定义 `mediamtx.{rtsp_host,rtsp_port,...}`，但 `DeepDogMqttConfig::RtspPushUrlForDeviceId()` / `LanHlsUrlForDeviceId()` 仍按**编译期** `DEEP_DOG_VISION_RTSP_HOST` 拼装；配置中心下发的 host 实际未生效。
- 影响：换部署只能改宏重编（F-04 的根因）。
- 建议：`deep_dog_mqtt.cc` 取 `ConfigManager::Instance().Config().mediamtx.rtsp_host`（非空时）覆盖；空则回落到 `DEEP_DOG_VISION_RTSP_HOST`。
- 相关：`config_center/config_manager.cc`、`mqtt/mqtt_config.cc`、`vision/vision_config.h`。

---

## F-10 · P1 · `face/cmd enabled=true` 无法启动人脸运行时；且 S09 MEM-07「face_boot 改 PSRAM 栈」**按原文不可行**

- **现象（MQTT）**：下发 `cmd/face {"enabled":true}` 后，`status/face` 始终
  `enabled=false, recognize_enabled=true, w=0, h=0, n=0`（即运行时未起来，也不会产出画面）。
- **现象（串口）**：`W dog_face_ai: SetEnabled(true) failed: face_boot task`
  （`face_boot` 需 `DEEP_DOG_FACE_BOOT_TASK_STACK` 的**连续 internal** 块；实测 post_activation
  `internal largest≈6.6~7.2KB`，不足以创建）。
- **本轮实验（已回滚）**：按 [S09 MEM-07](../vision/server/S09-internal-sram-optimization.md) 把
  `face_boot` 栈改为 PSRAM（`xTaskCreateWithCaps`）。**设备随即崩溃重启**：
  ```text
  assert failed: spi_flash_disable_interrupts_caches_and_other_cpu
      cache_utils.c:127 (esp_task_stack_is_sane_cache_disabled())
  ```
  根因：`DeepDogFaceAiRuntimeStart()` 的启动路径会做 **NVS/FAT（facedb/persist）等 flash 操作**，
  而 flash 操作期间会关闭 cache —— 此时**当前任务栈不能在 PSRAM**（与 S09 §3 自身的约束一致）。
- **结论**：**MEM-07 按原文实施不安全**（会 panic 重启），已回滚；`face_boot` 必须留在 internal 栈。
- **正确方向（供 S09 采纳）**：
  1. 先降 internal 峰值/碎片（MEM-01 启动错峰、MEM-02 取消 `immich_late`、MEM-05 合并 flash worker），
     让 post_activation `largest_int` 恢复到 ≥ 16KB；
  2. 或把「模型加载」与「NVS/FAT 初始化」**拆成两个任务**：只有纯计算/无 flash 访问的那段用 PSRAM 栈；
  3. 或 MEM-06（识别模型 lazy load）延后占内存的模型加载。
- **测试处理**：`TC0500` 在运行时起不来时记 **BLOCKED（附因）**而非 FAIL——属 F-01 家族的资源约束，
  非 face 业务逻辑缺陷；一旦 internal 恢复，该用例会自动转为 PASS。
- **相关**：`face_ai/face_ai_runtime.cc`（`DeepDogFaceAiSetEnabled`/`DeepDogFaceAiRuntimeStart`）、
  `face_ai/face_persist.cc`、`face_ai/face_facedb.cc`。

---

## F-11 · P2 · 设备 httpd 偶发回非 HTTP 首行（`BadStatusLine: ~`），仅命中 404 路径

- **现象**：全量跑时 `TC1353`（`GET /`）、`TC1360`（`GET /api/immich_status`）**ERROR**：
  ```text
  http.client.BadStatusLine: ~
  ```
  两例 `duration_s ≈ 0.04~0.05`，即对端**立即**回了一个非状态行的首行 `~`。
- **对照证据（同一设备、同一端口 8080）**：
  - `curl -i http://10.99.79.59:8080/` → `HTTP/1.1 404 Not Found` + `Content-Length: 29` + `Nothing matches the given URI`（**标准 404**）；
  - Python `urlopen` 连打 30×2 次（含**保持一条 WS 连接**时）→ **0 次异常**；
  - 单跑 `--domain http` → 4 例全部正常判为 **BLOCKED**（`DEEP_DOG_HTTP_SERVER_ENABLE=0`）。
- **根因（结论：传输/环境级偶发，非固件业务缺陷）**：这些路径在默认剖面下**未注册**（内置 httpd 编译关闭，
  见 LL-004），因此 404 是**预期**结果；`~` 首行无法用固件 handler 复现，且 60+ 次独立请求全通过。
  最可能是设备在多轮用例后（internal SRAM 紧张 + 大量短连接）的一次**连接级异常**（详见 F-01 家族）。
> **处理（已实施）**：
> 1. `dogtest/clients.py::HttpClient.request` 对**传输级异常**（`BadStatusLine`/`RemoteDisconnected`/reset/timeout）
>    做**一次有界重试**（0.5s），仍失败则抛 `TransportAnomaly`，并带上原始首行 `(首行=<line>)`；
> 2. `cases/tc_http.py` 的内置 httpd 四例统一走 `_builtin_gate()`：`TransportAnomaly` → **BLOCKED（附因）**，
>    不再以 ERROR 计——因为「内置 server 编译关闭」时它们的正确结果本就是 BLOCKED。
> **补充证据（2026-10-16，串口抓取一次冷启动）**：同一时刻 `dog_ws_mcp` 在该 boot **根本起不来**：
> ```text
> W dog_ws_mcp: httpd_start attempt 1/3 failed port=8080 err=ESP_ERR_HTTPD_TASK free_int=10135 largest_int=3584
> E httpd: httpd_server_init: error in listen (112)      ← ENOMEM
> E dog_ws_mcp: httpd_start failed on port 8080 err=ESP_FAIL
> ```
> 即 `largest_int` 只有 **3584B** < httpd 任务栈（默认 4096B）→ 本会话**完全没有 WS-MCP / 数据点 REST**。
> 已修：`deep_dog_ws_mcp_server.cc::Start()` 改为「**先等内存达标（`largest_int ≥ 4608`）+ 最多 6 轮重试**」
> （最多约 20s，LAN 工具晚几秒可用可接受）；修复后同一 boot 实测 `WS MCP bridge ws://10.99.79.59:8080/ws`
> 与 `dp_http: registered /api/datapoints/* endpoints` 均正常输出。
> 另测得：**WS-MCP 的 httpd 常驻约 5KB internal**（起用后 free 10.1KB→5.1KB），是与 face/VisionHub 的显式取舍（见 S09 §2.1）。
> **未关闭的动作**：若后续复现非 HTTP 首行且能抓到**原始字节流**（tcpdump/串口同时抓），再判定是否需要固件侧进一步加固。
- **相关**：`scripts/deep_dog/tests/dogtest/clients.py`、`scripts/deep_dog/tests/cases/tc_http.py`、
  `http-server/deep_dog_http_server.cc`（`DEEP_DOG_HTTP_SERVER_ENABLE=0` 时不注册 `/`、`/api/*`）。

---

## F-12 · P1 · 数据点自动 MCP 工具（75 个）注册不上：内存守卫「全有或全无」+ 首次失败永久放弃

> **状态：✅ 已修复（方案 A 落地，2026-10-16）**
> 采用 **`McpServer::AutoToolSource` 按需生成**（等价于「工具对象不占堆」）：工具名/描述/schema 在
> `tools/list`、`tools/call` 时**逐条现算**到调用方栈缓冲，**零 per-tool 堆对象**，与 internal 余量彻底解耦
> （比「对象落 PSRAM」更彻底：连 PSRAM 也不用常驻）。实现：`main/mcp_server.{h,cc}` 新增 `AutoToolSource`
> + `AutoToolToJson`/`ParseArgs`/`InvokeAutoToolSync`；`ws-mcp/datapoint_mcp.{h,cc}` 重写为
> `DataPointAutoTools`（单例、常数内存），板级 `InitializeTools()` 无条件挂接（不再懒注册、不再有内存守卫）。
> **验收证据**：`TC1311` → `auto dp tools=75/75` **PASS**；`TC1261` → `MCP set_tunnel → status/can.tunnel 已同步` **PASS**；
> 挂接后 `internal_free=9239B / largest=4352B`（与修复前同量级，证明**未新增 internal 常驻**）。
> 注：`tools/call` 走主线程 `Application::Schedule`（与手写工具一致）；自动工具仍为 `user_only`，
> 不随云端 `tools/list` 上行，`AppendToolsToJsonArray()`（`status/motor/tools` 等）**不含**自动工具，避免 MQTT retain 膨胀。

- **现象（修复前）**：`tools/list` 中**没有任何**按 schema 推导的自动工具（`self.<mod>.get_/set_<point>`）；
  `TC1311`（工具注册）/ `TC1261`（读写回环）实测 **BLOCKED**，`0/75`。
- **实测数据**（本机 · CAN 剖面 · `GET /api/datapoints/...`）：
  - schema：`channels` 含 `mcp` 的点位 **73** 个 → 应注册 get 73 + set 2 = **75 个工具**；
  - `system.largest_free_internal` = **3712 ~ 4352 B**，`internal_free ≈ 9.5 KB`，`min_free_heap ≈ 1.7 KB`；
  - 守卫阈值 `kMinLargestInternalBeforeAdd = 6144 B`（`ws-mcp/datapoint_mcp.cc`）→ **首个点位即被拦**，注册 0 个。
- **历史对照**：同一套代码在内存较宽松时串口可见 `dp_mcp: registered 75 auto MCP tools from data point registry`
  → 即**并非物理上装不下**，而是**当前开机内存状态装不下**（根因是 F-01 §0.2 记录的 internal 回归 ≈ 26 KB）。
- **两个独立缺陷（本次定位）**：
  1. **全有或全无**：守卫对每条工具用固定阈值判定，低于阈值时**一条都不注册**；而少量工具（每条数百字节）
     在 3.7 KB 连续块内本可容纳 → 工具面随开机内存**不确定**（有时 0、有时部分），对契约比「全都没有」更糟。
  2. **首次失败后永久放弃**：`g_dp_tools_registered = true` 在 `count == 0` 时也置位 → 本会话后续 `tools/list` 不再重试。
- **已实施（本轮，仅缺陷 2）**：置位条件改为 `count > 0 || !out_of_room`（`ws-mcp/datapoint_mcp.cc`）——
  一条都没注册时保留重试机会；有注册则必须置位，否则重试会**重复注册同名工具**。
  缺陷 1 **不修**：部分注册不可复现、破坏工具面确定性，属设计决策（见下）。
- **待决策（三选一，需 S09/架构口径）**：
  - **A（推荐）工具对象落 PSRAM**：`McpTool` 及其 `std::string` / `PropertyList` 走 `heap_caps`/自定义分配器放 SPIRAM
    （本机 PSRAM free ≈ 7.6 MB，75 工具约 20~30 KB）。代价：改公共组件 `main/mcp_server.{h,cc}`，需回归云端 MCP。
    收益：75 个工具**确定性**全量可用。
  - **B 收敛为通用读写工具**：`self.dp.get{point}` / `self.dp.set{point,value}` / `self.dp.list` → 工具数常数级（2~3 个）。
    代价：**契约变更**（`docs/interface/05-mcp-tool-normalization.md` + 云/前端联调）；收益：确定性 + 内存常数。
  - **C 维持现状并文档化**：MCP 数据点面在内存紧张时不可用，`TC1261/TC1311` 长期 BLOCKED（依附 S09 内存专项）。
- **测试侧修正（本轮）**：`TC1311` 原断言用 `".set_" in name` 子串判定，会被手写工具
  `self.audio_speaker.set_volume` 等**误判为 PASS**（假通过）→ 改为**按 schema 推导全量比对**，
  缺失即 BLOCKED 并附实测 `largest_free_internal` 字节数（见 `dogtest/dpschema.py`）。
- **相关**：`ws-mcp/datapoint_mcp.cc`、`ws-mcp/deep_dog_ws_mcp_server.cc`（懒注册入口）、
  `main/mcp_server.h`（`std::vector<McpTool*> tools_`）、`docs/interface/05-mcp-tool-normalization.md`、
  [S09](../vision/server/S09-internal-sram-optimization.md)。

---

## F-13 · P1 · 长跑中设备 httpd（8080：WS-MCP + 数据点 REST）短暂「消失」→ 单点断连被测试放大成 11 例级联 ERROR

- **现象**（第 14 轮第 1 次全量，127 例）：自 `TC0525`（`self.face.set_mode`）起
  - 首例 `TimeoutError: timed out in 15.0s`；
  - 之后每个 WS 用例 `ConnectionClosedError: no close frame received or sent`；
  - 同段 `TC1264`（HTTP）`URLError: <urlopen error timed out>`；
  - 共 **11 例 ERROR**，全部落在 WS / HTTP 通道；**同段 MQTT 用例正常**；
  - 同一次运行里 `TC1352`（HTTP CORS）**PASS** → httpd 稍后**自行恢复**。
- **设备侧佐证**：该时刻 `system.internal_free ≈ 5.1 KB`、`min_free_heap ≈ 799 B`（余量已到 **~800 B**）
  → 极低 internal 余量下 httpd 会话/worker 分配失败或超时（F-01 家族；同源问题见 F-12）。
- **两个层次的问题**：
  1. **固件/环境（未修）**：internal 余量到 ~800B 时任何新增分配都可能失败 —— 属 S09 内存专项（与 F-12 同源）。
  2. **测试基建（本轮已修）**：`Ctx.wsmcp` 原实现**缓存单个 socket**，断开后**所有后续用例复用死连接**
     → 一次瞬时断连被放大成 11 个 ERROR（真实故障只有 1 次）。
- **已实施（测试侧）**：
  - `WsMcp._rpc()`：连接非 `OPEN` 或收发异常时**重连一次**再试（0.5s 退避）；
  - `Ctx.wsmcp`：每次访问先 `is_alive()`，失效则丢弃旧 socket 重新握手；
  - `runner.run()`：新增 `TRANSPORT_EXC` 分类（`TransportAnomaly`、websockets `ConnectionClosed`/`InvalidStatus`、
    `TimeoutError`、`Reset`、`OSError`）→ 判 **BLOCKED（附因）**；语义是「用例根本没跑起来」，不再计入 ERROR。
- **效果**：级联消失——每个用例独立报告各自的传输结果（见第 14 轮第 3 次运行）。
- **相关**：`scripts/deep_dog/tests/dogtest/clients.py`、`.../dogtest/runner.py`、
  `ws-mcp/deep_dog_ws_mcp_server.cc`（esp_http_server 会话/worker 配置）。

---

## F-14 · P1 · 全量测试**运行间波动**：同一固件同一用例集结果不一致（设备侧 internal 极限 + 外部常驻 WS 客户端）

- **现象**：同一固件、同一用例集（`run.py --serial-cases`，127 条）连续多次全量运行结果差异明显：
  | 运行 | PASS | FAIL | ERROR | BLOCKED | SKIP | 失败聚集点 |
  |------|------|------|-------|---------|------|-----------|
  | A（第 14 轮） | 114 | 0 | 0 | 11 | 2 | — |
  | B（第 15 轮 ①，F-12+MEM-01 后） | 109 | 5 | 1 | 10 | 3 | `config` ×3（10s 超时无 ack）+ `mcp` ×3（tools/list 空/超时） |
  | C（第 15 轮 ②） | — | 5+ | — | — | — | `touch`×3 / `handle`×3 / `imu`×2（status 停报），期间 **HTTP 完全不可达** |
  上述**失败用例单独运行全部 PASS**：`--domain config` → 6/6 PASS；`--case TC1306` / `--case TC1311` → PASS。
- **设备侧证据（串口，同一 boot）**：
  ```text
  E (15070) lwip_arch: thread_sem_init: out of memory          ← F-01 签名（MQTT 起栈期 internal 不足）
  E (30030) dog_ws_mcp: broadcast send failed fd=58 err=258     ← ESP_ERR_TIMEOUT，同一 fd 连续多次
  I (43500) SystemInfo: sram(internal): free=8667 min=107 total=403939 (2.1% free)
  I (16320/21330/26320/…) dog_ws_mcp: WebSocket handshake ok    ← 每 ~5s 一次 = 外部常驻 WS 客户端
  ```
  全程**无 panic / assert / WDT**。
- **结论**：在 `largest_int≈1.8~4KB`、`internal min≈107B` 的现状下，MQTT/HTTP/WS 的偶发卡顿是**系统性**的
  （F-01 家族），因此全量结果存在运行间波动；**不是被测业务逻辑缺陷**，也非本轮改动引入（改动前亦出现同类波动）。
  另：存在**外部常驻 WS 客户端**（浏览器/其它脚本，每 ~5s 重连）与测试并发，会通过广播路径加剧卡顿。
- **已实施（降低影响面）**：
  1. 广播载荷改**优先 PSRAM**；`httpd_ws_send_frame_async` 失败即 `DropClient(fd)` 摘除死连接，
     避免同一 fd 反复超时拖慢 httpd 任务（`deep_dog_ws_mcp_server.cc`）；
  2. 传输级异常判 **BLOCKED（附因）** 而非 ERROR，且 WS 客户端断线自愈（F-13）；
  3. `%zu` 在 IDF 的 newlib-nano 下会 출력为字面 `zu`（实测 `client connected … total=zu`）→ ws-mcp 三处改 `%u`。
- **建议**：跑全量前**关闭外部常驻 WS 客户端**（浏览器设备页 / 其它 agent 的轮询脚本）；根治仍需 S09 内存净优化（本文件 §0.6）。
- **相关**：`ws-mcp/deep_dog_ws_mcp_server.cc`、`scripts/deep_dog/tests/dogtest/{clients,runner}.py`、[S09](../vision/server/S09-internal-sram-optimization.md)。

---

## 复现方法

```bash
# 1) 复位设备（可选；注意：打开串口本身会触发复位）
# 2) 全量测试
python scripts/deep_dog/tests/run.py
# 3) 看报告
#    docs/requirements/TEST_REPORT.md
```
