# deep-dog 功能需求注册表（FEATURES）

> **唯一真源**：本文件是 deep-dog 板「功能点编号」的权威注册表。
> 编号规则见 [CONVENTIONS.md §2.1](../../CONVENTIONS.md)；测试用例见 [TESTS.md](./TESTS.md)。
> 功能点正文仍在各域需求文档（`出处` 列）；本表只做**编号 + 归属 + 状态 + 追溯**。

## 图例

- **状态**：`已实现` · `部分实现` · `占位` · `待办`（文档有/代码无） · `未见`（仅契约，无实现）
- **通道**（可观测路径）：`MQTT` · `HTTP` · `WS-MCP` · `RTSP` · `串口` · `内部`
- **出处**路径相对 `main/boards/deep-dog/docs/requirements/`。

---

## device · 设备信息 / 心跳 / 内存 / 诊断（F0001–F0099）

| F-ID | 功能点 | slug | 路线图ID | 出处 | 通道 | 状态 |
|------|--------|------|----------|------|------|------|
| F0001 | 上线发布 `info/device` retain（含 firmware/board/ip/capabilities/ext_pins） | device-info-publish | M01 | mqtt/modules/01-device.md | MQTT | 已实现 |
| F0002 | 周期发布 `status/device` 心跳（约 5s） | device-heartbeat | M01 | mqtt/modules/01-device.md | MQTT | 已实现 |
| F0003 | `mem.internal/psram` 分项（free/min/total/largest_free/used） | device-mem-report | V-S09 | mqtt/modules/01-device.md | MQTT | 已实现 |
| F0004 | `health.ok` + `warn[low_internal_heap]`（阈值 32KiB） | device-health-warn | S09 | mqtt/modules/01-device.md | MQTT | 已实现 |
| F0005 | `clock_synced` + `warn[clock_unsynced]` | device-clock-synced | N01 | mqtt/modules/01-device.md | MQTT | 已实现 |
| F0006 | `tasks[]` / `task_count`（按 stack_hwm 升序） | device-task-stacks | V-S08 | mqtt/modules/01-device.md | MQTT | 已实现 |
| F0007 | 设钟后重发 device 信息 + status/face/registry | device-clock-resync | N01 | mqtt/modules/01-device.md | MQTT | 已实现 |
| F0008 | device 数据点注册（12 个 `device.*`） | device-datapoints | DP-01 | interface/04-datapoint-registry.md | MQTT | 已实现 |
| F0009 | WiFi SSID/信道仅入 `status`（不进 retain info） | device-wifi-status | M01 | mqtt/modules/01-device.md | MQTT | 已实现 |
| F0010 | `power.supported=false`（不发伪电量） | device-power-unsupported | M01 | mqtt/modules/01-device.md | MQTT | 已实现 |
| F0011 | `capabilities.*` 能力位（按 ENABLE/生效模式计算） | device-capabilities | CFG-02 | mqtt/modules/01-device.md | MQTT | 已实现 |
| F0012 | system 数据点注册（8 个 `system.*`） | device-system-datapoints | DP-01 | interface/04-datapoint-registry.md | MQTT | 已实现 |
| F0013 | 串口内存报告 `DeepDogMemoryReportLog(phase)` | device-mem-report-serial | S09 | ll/LL-005-restore-headers-and-union-size-measure.md | 串口 | 已实现 |
| F0014 | MCP `self.board.get_ip`（ip/connected/ws_url） | device-mcp-get-ip | M02 | mqtt/M02-mcp-call-control-plane.md | WS-MCP | 已实现 |
| F0015 | MCP `self.board.diagnostics`（内存 + 任务栈） | device-mcp-diagnostics | M02 | interface/05-mcp-tool-normalization.md | WS-MCP | 已实现 |
| F0016 | MQTT topic 骨架 `{prefix}/{device_id}/{kind}/{subject}` | mqtt-topic-skeleton | M01 | mqtt/M01-board-mqtt-protocol.md | MQTT | 已实现 |
| F0017 | `device_id` 恒为 MAC 紧凑小写（无 `dev` 身份） | mqtt-device-id-mac | M01 | mqtt/M01-board-mqtt-protocol.md | MQTT | 已实现 |
| F0018 | `client_id` 随机后缀防会话互踢 | mqtt-client-id-random | M01 | mqtt/M01-board-mqtt-protocol.md | MQTT | 已实现 |
| F0019 | `topic_root` 可配置（编译默认 `deep-trace` + NVS） | mqtt-topic-root | DT-01 | interface/01-mqtt-contract.md | MQTT | 已实现 |
| F0020 | 上线/重连重发全部 retain 状态 + `info/schema` | mqtt-retain-republish | M01 | mqtt/M01-board-mqtt-protocol.md | MQTT | 已实现 |
| F0021 | `cmd/<module>` 经数据点注册表校验写入 | mqtt-cmd-datapoint-write | DP-00 | mqtt/M01-board-mqtt-protocol.md | MQTT | 已实现 |
| F0022 | `info/schema` retain 发布数据点 schema | mqtt-info-schema | DP-00 | mqtt/M01-board-mqtt-protocol.md | MQTT | 已实现 |
| F0023 | QoS 约定（cmd=1 / status=0 / retain 策略） | mqtt-qos-conventions | M01 | mqtt/M01-board-mqtt-protocol.md | MQTT | 已实现 |
| F0024 | 断线重连（约 3s，串行化 stop/destroy/init） | mqtt-reconnect | M01 | mqtt/M01-board-mqtt-protocol.md | 串口 | 已实现 |
| F0025 | 板级 MQTT 开启时 WiFi 禁 LOW_POWER（`WIFI_PS_NONE`） | mqtt-wifi-perf-mode | M01 | mqtt/M01-board-mqtt-protocol.md | 串口 | 未见 |

---

## pair · 设备配对（F0100–F0199）

| F-ID | 功能点 | slug | 路线图ID | 出处 | 通道 | 状态 |
|------|--------|------|----------|------|------|------|
| F0100 | 上报 `status/pairing` retain（device_id/mac/bound） | pair-status-retain | DT-02 | mqtt/modules/00-pairing.md | MQTT | 已实现 |
| F0101 | 未绑定开机静默（不自动播码） | pair-unbound-silent | DT-02 | mqtt/modules/00-pairing.md | 串口 | 已实现 |
| F0102 | 生成 6 位码 + 屏显 + 语音逐位播报 | pair-announce-code | DT-02 | mqtt/modules/00-pairing.md | 串口 | 已实现 |
| F0103 | 配对会话内 45s 重播 | pair-replay-45s | DT-02 | mqtt/modules/00-pairing.md | MQTT | 已实现 |
| F0104 | `cmd/pairing action=bound` 写 NVS、停重播 | pair-cmd-bound | DT-02 | mqtt/modules/00-pairing.md | MQTT | 已实现 |
| F0105 | `cmd/pairing action=unbind` 清 bound、保留 MAC | pair-cmd-unbind | DT-02 | mqtt/modules/00-pairing.md | MQTT | 已实现 |
| F0106 | `cmd/pairing action=start_pairing` | pair-cmd-start | DT-02 | mqtt/modules/00-pairing.md | MQTT | 已实现 |
| F0107 | `event/pairing/request action=unbind` | pair-event-unbind | DT-02 | mqtt/modules/00-pairing.md | MQTT | 已实现 |
| F0108 | pro 模式未绑定也 retain 带 code | pair-pro-mode | DT-02 | mqtt/modules/00-pairing.md | MQTT | 已实现 |
| F0109 | 身份切换 defer 主循环重载客户端 | pair-reload-defer | DT-02 | mqtt/modules/00-pairing.md | 内部 | 已实现 |
| F0110 | MCP `self.device.start_pairing/cancel_pairing/unbind` | pair-mcp-tools | M02 | interface/05-mcp-tool-normalization.md | WS-MCP | 已实现 |
| F0111 | 触摸 `hold1_tap2` 配对 / `hold1_tap3` 解绑 | pair-touch-combo | M01 | mqtt/modules/00-pairing.md | 串口 | 已实现 |
| F0112 | HTTP 握手 `POST /devices/pair/handshake` | pair-http-handshake | DT-02 | mqtt/modules/16-http-pairing.md | HTTP | 已实现 |
| F0113 | HTTP 轮询 `POST /devices/pair/activate` | pair-http-activate | DT-02 | mqtt/modules/16-http-pairing.md | HTTP | 已实现 |
| F0114 | 200 应用身份（bound/device_id/broker）→ NVS | pair-http-apply-identity | DT-02 | mqtt/modules/16-http-pairing.md | HTTP | 已实现 |
| F0115 | 429/404/410/401 终止与退避 | pair-http-error-states | DT-02 | mqtt/modules/16-http-pairing.md | HTTP | 已实现 |
| F0116 | Broker 回环地址防护（127.0.0.1/localhost） | pair-http-broker-guard | DT-02 | mqtt/modules/16-http-pairing.md | HTTP | 已实现 |
| F0117 | `server_api_base` 非空优先走 HTTP 配对 | pair-http-select | DT-02 | mqtt/modules/16-http-pairing.md | HTTP | 已实现 |
| F0118 | HTTP 解绑 `POST /devices/pair/unbind` | pair-http-unbind | DT-02 | interface/03-http-api-contract.md | HTTP | 部分实现 |
| F0119 | 契约决策 D4：移除 `status/pairing`/`event/pairing/request` 上行 | pair-deprecate-mqtt-ingest | — | interface/01-mqtt-contract.md | 内部 | 待办 |

---

## net · 网络（F0200–F0299）

| F-ID | 功能点 | slug | 路线图ID | 出处 | 通道 | 状态 |
|------|--------|------|----------|------|------|------|
| F0200 | WiFi 获 IP 后启动 SNTP 同步 UTC | net-sntp-init | N01 | net/N01-sntp-clock-sync.md | 串口 | 已实现 |
| F0201 | 时钟可信判定 `time()>=1e9` | net-clock-synced-check | N01 | net/N01-sntp-clock-sync.md | 内部 | 已实现 |
| F0202 | 已同步返回 Unix 秒 / 未同步回落 boot 秒 | net-now-unix-sec | N01 | net/N01-sntp-clock-sync.md | 内部 | 已实现 |
| F0203 | 首次同步回调（重发 registry/status） | net-sntp-on-synced-cb | N01 | net/N01-sntp-clock-sync.md | 串口 | 已实现 |
| F0204 | 主/备 NTP 服务器可配 | net-sntp-servers-config | N01 | net/N01-sntp-clock-sync.md | 内部 | 已实现 |
| F0205 | SNTP 可关闭（仅 OTA 设钟） | net-sntp-disable | N01 | net/N01-sntp-clock-sync.md | 内部 | 已实现 |
| F0206 | STA 默认 DHCP 获取 IP | net-sta-dhcp | N02 | net/N02-sta-address.md | 串口 | 已实现 |
| F0207 | WS MCP 听 `0.0.0.0:8080` 不绑定网段 | net-ws-listen-any | N02 | net/N02-sta-address.md | WS-MCP | 已实现 |
| F0208 | `info/device` 广播 `ip/ws_mcp_port/ws_mcp_path` | net-device-info-ip | N02 | net/N02-sta-address.md | MQTT | 已实现 |
| F0209 | 静态 IP 可选（固定实验室） | net-sta-static-ip | N02 | net/N02-sta-address.md | 内部 | 已实现 |
| F0210 | 配网页可设置板级 MQTT broker/port | net-portal-mqtt-broker | N03 | net/N03-wifi-portal-mqtt-broker.md | HTTP | 已实现 |
| F0211 | 配网页恢复自定义 OTA URL 可见性 | net-portal-ota-url | N03 | net/N03-wifi-portal-mqtt-broker.md | HTTP | 已实现 |
| F0212 | 配网模式跳过 STA IP 死等 | net-portal-skip-sta-wait | N03 | net/N03-wifi-portal-mqtt-broker.md | 串口 | 已实现 |
| F0213 | Broker 废弃地址自动迁移 | net-broker-migrate | N03 | net/N03-wifi-portal-mqtt-broker.md | 串口 | 已实现 |

---

## config · 统一配置中心 / 引出脚运行时（F0300–F0399）

| F-ID | 功能点 | slug | 路线图ID | 出处 | 通道 | 状态 |
|------|--------|------|----------|------|------|------|
| F0300 | HTTP 拉取配置 `POST /devices/config/pull` | config-pull | CFG-01 | config/CFG-01-unified-config-center.md | HTTP | 已实现 |
| F0301 | 版本相同返回简化响应（304 语义） | config-pull-not-modified | CFG-01 | config/CFG-01-unified-config-center.md | HTTP | 已实现 |
| F0302 | 生效确认 Ack（success/partial/failed + errors[]） | config-ack | CFG-01 | config/CFG-01-unified-config-center.md | HTTP | 已实现 |
| F0303 | 设备注册拿 `device_secret` | config-register | CFG-01 | config/CFG-01-unified-config-center.md | HTTP | 已实现 |
| F0304 | HMAC-SHA256 设备自签名（三头） | config-hmac-sign | CFG-01 | interface/03-http-api-contract.md | HTTP | 已实现 |
| F0305 | 开发期可跳过签名降级（secret 空） | config-auth-degrade | CFG-01 | config/CFG-01-unified-config-center.md | HTTP | 已实现 |
| F0306 | MQTT `cmd/config` 通知触发立即拉取 | config-mqtt-notify-pull | CFG-01 | mqtt/modules/17-config.md | MQTT | 已实现 |
| F0307 | MQTT 收通知即时回 `cmd/config/ack` | config-mqtt-ack | CFG-01 | mqtt/modules/17-config.md | MQTT | 已实现 |
| F0308 | HTTP 定时轮询兜底（默认 300s） | config-http-poll | CFG-01 | config/CFG-01-unified-config-center.md | HTTP | 已实现 |
| F0309 | 失败指数退避重试（30s→300s） | config-backoff-retry | CFG-01 | config/CFG-01-unified-config-center.md | 串口 | 已实现 |
| F0310 | 服务器不可达时用 NVS 缓存降级 | config-nvs-cache-degrade | CFG-01 | config/CFG-01-unified-config-center.md | 内部 | 已实现 |
| F0311 | 旧 NVS（`deep_dog_mqtt`）迁移合并 | config-legacy-nvs-migrate | CFG-01 | config/CFG-01-unified-config-center.md | 内部 | 已实现 |
| F0312 | 配置差异观察者分发（只通知变更路径） | config-change-dispatch | CFG-01 | config/CFG-01-unified-config-center.md | 内部 | 已实现 |
| F0313 | MQTT 配置变更 → 模块热重连 | config-mqtt-hot-reload | CFG-01 | config/CFG-01-unified-config-center.md | 内部 | 部分实现 |
| F0314 | 全量配置解析（connectivity/mediamtx/api_server/io） | config-parse-json | CFG-01 | config/CFG-01-unified-config-center.md | 内部 | 已实现 |
| F0315 | 流路径模板解析（代入 device_id） | config-resolve-stream-path | CFG-01 | config/CFG-01-unified-config-center.md | 内部 | 已实现 |
| F0316 | 配置项点分路径读取 API | config-get-by-path | CFG-01 | config/CFG-01-unified-config-center.md | 内部 | 已实现 |
| F0317 | ext_pins 下发写 pending（重启生效） | extpin-set-pending | CFG-02 | config/CFG-02-ext-pin-runtime-mode.md | HTTP | 已实现 |
| F0318 | 非法 mode 拒绝回滚（`E_UNSUPPORTED_MODE`） | extpin-reject-mode | CFG-02 | config/CFG-02-ext-pin-runtime-mode.md | HTTP | 已实现 |
| F0319 | `gpio_a/gpio_b` 只读（改则 `E_RO_PIN`） | extpin-ro-pin | CFG-02 | config/CFG-02-ext-pin-runtime-mode.md | HTTP | 已实现 |
| F0320 | 启动目标解析（pending > active > 编译默认） | extpin-resolve-boot | CFG-02 | config/CFG-02-ext-pin-runtime-mode.md | 串口 | 已实现 |
| F0321 | 应用成功提交 active / 清 pending | extpin-commit | CFG-02 | config/CFG-02-ext-pin-runtime-mode.md | 串口 | 已实现 |
| F0322 | 启动失败自动回滚（含 none 兜底） | extpin-rollback | CFG-02 | config/CFG-02-ext-pin-runtime-mode.md | 串口 | 已实现 |
| F0323 | 白名单 `supported_modes`（随编译裁剪） | extpin-supported-modes | CFG-02 | config/CFG-02-ext-pin-runtime-mode.md | MQTT | 已实现 |
| F0324 | `pending_mode` / `apply_policy`（固定 restart）上报 | extpin-pending-report | CFG-02 | config/CFG-02-ext-pin-runtime-mode.md | MQTT | 已实现 |
| F0325 | 全模式编入 + 运行时选择（P1） | extpin-runtime-select | CFG-02 | config/CFG-02-ext-pin-runtime-mode.md | 串口 | 已实现 |
| F0326 | 后端模板接入 `io.ext_pins` | extpin-backend-template | CFG-02 | config/CFG-02-ext-pin-runtime-mode.md | 内部 | 待办 |
| F0327 | 全模式实机回归 | extpin-regress | CFG-02 | config/CFG-02-ext-pin-runtime-mode.md | 串口 | 待办 |

---

## stream · 推流 / MJPEG / 拍照 / 采集（F0400–F0499）

| F-ID | 功能点 | slug | 路线图ID | 出处 | 通道 | 状态 |
|------|--------|------|----------|------|------|------|
| F0400 | `cmd/stream start`（缺省→rtsp_push） | stream-cmd-start | V-C03 | mqtt/modules/02-stream.md | MQTT | 已实现 |
| F0401 | `cmd/stream stop`（恒强制 off） | stream-cmd-stop | V-C03 | mqtt/modules/02-stream.md | MQTT | 已实现 |
| F0402 | `cmd/stream mode=stream`（HTTP MJPEG） | stream-mode-mjpeg | V-C03 | mqtt/modules/02-stream.md | MQTT | 已实现 |
| F0403 | 非法/缺 action 回错误码且不崩溃 | stream-cmd-errors | V-C03 | mqtt/modules/02-stream.md | MQTT | 已实现 |
| F0404 | `status/stream` retain（state/mode/url/error/voice_paused） | stream-status | V-C03 | mqtt/modules/02-stream.md | MQTT | 已实现 |
| F0405 | RTSP 推流超时自动停（约 5min） | stream-auto-stop | V-S08 | mqtt/modules/02-stream.md | MQTT | 已实现 |
| F0406 | `take_photo` → `event/stream/photo` | stream-take-photo | V-C03 | mqtt/modules/02-stream.md | MQTT | 已实现 |
| F0407 | 并发拍照 `busy` / `task_fail` / `oom` | stream-take-photo-errors | V-C03 | mqtt/modules/02-stream.md | MQTT | 已实现 |
| F0408 | 推流态与真实 pusher 一致（含 no_rtp） | stream-state-fidelity | V-C02 | mqtt/modules/02-stream.md | MQTT | 已实现 |
| F0409 | stream `mcp_call`（`self.camera.*`） | stream-mcp-call | M02 | mqtt/M02-mcp-call-control-plane.md | MQTT | 待办 |
| F0410 | HTTP `GET /stream` 局域网 MJPEG 可拉取 | stream-http-mjpeg | V-S02 | vision/server/S02-http-mjpeg.md | HTTP | 已实现 |
| F0411 | 采集三态 off/periodic/stream | stream-capture-three-modes | V-S02 | vision/server/S02-http-mjpeg.md | HTTP | 已实现 |
| F0412 | `POST /api/capture_mode?mode=` 切换 | stream-capture-mode-api | V-S02 | vision/server/S02-http-mjpeg.md | HTTP | 已实现 |
| F0413 | `/api/status`（mode/stream_clients/has_jpeg/port/push_status） | stream-api-status | V-S02 | vision/server/S02-http-mjpeg.md | HTTP | 已实现 |
| F0414 | 并发拉流受限、满则 503 | stream-503-concurrency | V-S02 | vision/server/S02-http-mjpeg.md | HTTP | 已实现 |
| F0415 | `/stream` 独立任务不堵 `/api/cmd` | stream-isolation | V-S02 | vision/server/S02-http-mjpeg.md | HTTP | 已实现 |
| F0416 | 采集 640×480（§9 双分辨率剖面） | stream-640x480-capture | V-S06 | vision/server/S06-higher-resolution.md | HTTP | 部分实现 |
| F0417 | RTSP 推流前缩至 320×240 | stream-rtsp-320x240 | V-S06 | vision/server/S06-higher-resolution.md | RTSP | 已实现 |
| F0418 | `POST /api/vision_publish`（capture_mode 别名） | stream-vision-publish-api | V-S02 | vision/server/S02-http-mjpeg.md | HTTP | 已实现 |
| F0419 | 可配置开机推流（默认关） | stream-push-at-boot | V-C02 | vision/client/C02-device-push-stream.md | 串口 | 已实现 |
| F0420 | 推流路径/URL 可配 | stream-configurable-url | V-C02 | vision/client/C02-device-push-stream.md | MQTT | 已实现 |
| F0421 | 断网指数退避重连 | stream-exponential-backoff | V-C02 | vision/client/C02-device-push-stream.md | 串口 | 已实现 |
| F0422 | MJPEG 与 RTSP 互斥 | stream-mjpeg-rtsp-mutex | V-C02 | vision/client/C02-device-push-stream.md | MQTT | 已实现 |
| F0423 | JPEG over RTP 回退路径 | stream-jpeg-fallback | V-C02 | vision/client/C02-device-push-stream.md | RTSP | 已实现 |
| F0424 | 推流时人脸管线永驻（静默检测） | stream-face-always-on | V-C02 | vision/client/C02-device-push-stream.md | MQTT | 已实现 |
| F0425 | `DESCRIBE` 为 H264/90000 | stream-describe-h264 | V-C02 | vision/client/C02-device-push-stream.md | RTSP | 已实现 |
| F0426 | `voice_paused` 推流暂停语音 | stream-voice-paused | V-S08 | vision/server/S08-rtsp-face-wdt-mitigation.md | MQTT | 已实现 |
| F0427 | 推流暂停 AFE/唤醒、结束恢复 | stream-afe-pause-resume | V-S08 | vision/server/S08-rtsp-face-wdt-mitigation.md | 串口 | 已实现 |
| F0428 | 推流时人脸送帧间隔下限 2000ms | stream-rtsp-min-interval | V-S08 | vision/server/S08-rtsp-face-wdt-mitigation.md | 串口 | 已实现 |
| F0429 | 推流时识别最小间隔 4000ms | stream-rtsp-recog-min-interval | V-S08 | vision/server/S08-rtsp-face-wdt-mitigation.md | 串口 | 已实现 |

---

## face · 人脸检测 / 识别 / 库 / MCP（F0500–F0599）

| F-ID | 功能点 | slug | 路线图ID | 出处 | 通道 | 状态 |
|------|--------|------|----------|------|------|------|
| F0500 | `cmd/face enabled` 检测总开关（冷启动懒加载） | face-enable | V-S07 | mqtt/modules/04-face.md | MQTT | 已实现 |
| F0501 | `cmd/face recognize_enabled` | face-recognize-toggle | V-S07 | mqtt/modules/04-face.md | MQTT | 已实现 |
| F0502 | `cmd/face pipeline=live/identity` | face-set-mode | V-S07 | mqtt/modules/04-face.md | MQTT | 已实现 |
| F0503 | `cmd/face detect_interval_ms`（200–5000 夹紧） | face-detect-interval | V-S07 | mqtt/modules/04-face.md | MQTT | 已实现 |
| F0504 | `action=clear_db` 清库 | face-clear-db | V-S07 | mqtt/modules/04-face.md | MQTT | 已实现 |
| F0505 | `action=rename` | face-rename | V-S07 | mqtt/modules/04-face.md | MQTT | 已实现 |
| F0506 | `action=delete_one` | face-delete-one | V-S07 | mqtt/modules/04-face.md | MQTT | 已实现 |
| F0507 | `action=merge`（alias 合并） | face-merge | V-S07 | mqtt/modules/04-face.md | MQTT | 已实现 |
| F0508 | `action=refresh_status`（立即回一帧） | face-refresh-status | V-S07 | mqtt/modules/04-face.md | MQTT | 已实现 |
| F0509 | `action=refresh_immich` | face-refresh-immich | V-S07 | mqtt/modules/04-face.md | MQTT | 已实现 |
| F0510 | `status/face`（框/primary/faces/w/h/n，on_change + 30s 心跳） | face-status | V-S07 | mqtt/modules/04-face.md | MQTT | 已实现 |
| F0511 | `status/face/registry` retain（版本/人数/槽位/entries） | face-registry-retain | V-S07 | vision/server/S07-face-control-mcp.md | MQTT | 已实现 |
| F0512 | 变更开关后 republish registry | face-registry-republish | V-S07 | mqtt/modules/04-face.md | MQTT | 已实现 |
| F0513 | 库容 32 + LRU 淘汰 | face-recog-max-lru | V-S07 | vision/server/S04-local-face-numeric-id.md | 串口 | 已实现 |
| F0514 | 未入库人脸自动建档、分配数字 ID | face-auto-enroll-id | V-S04 | vision/server/S04-local-face-numeric-id.md | HTTP | 已实现 |
| F0515 | 同人 5s 会话 / NVS 命中保持同一 ID | face-session-nvs-stable-id | V-S04 | vision/server/S04-local-face-numeric-id.md | HTTP | 已实现 |
| F0516 | 断电后 NVS 恢复 ID（meta_ver=4） | face-nvs-persist-recovery | V-S04 | vision/server/S04-local-face-numeric-id.md | 串口 | 已实现 |
| F0517 | 同帧最多识别 N 张（默认 4） | face-multi-max | V-S04 | vision/server/S04-local-face-numeric-id.md | HTTP | 已实现 |
| F0518 | `recognize_source` = session/nvs/enrolled/none | face-recognize-source | V-S04 | vision/server/S04-local-face-numeric-id.md | HTTP | 已实现 |
| F0519 | 满库 LRU 淘汰最久未见槽 | face-lru-evict | V-S04 | vision/server/S04-local-face-numeric-id.md | 串口 | 已实现 |
| F0520 | `/api/face` 返回框 + has_face + w/h + faces[] | face-http-api | V-S03 | vision/server/S03-http-face-overlay.md | HTTP | 已实现 |
| F0521 | `POST /api/face_enable?enabled=` | face-http-enable | V-S03 | vision/server/S03-http-face-overlay.md | HTTP | 已实现 |
| F0522 | 网页 Canvas 叠框（轮询） | face-web-canvas-overlay | V-S03 | vision/server/S03-http-face-overlay.md | HTTP | 已实现 |
| F0523 | 非 Streaming / 关人脸停送帧推理 | face-stop-when-off | V-S03 | vision/server/S03-http-face-overlay.md | 串口 | 已实现 |
| F0524 | 暗场误检抑制（SKIP_UNIFORM_DARK + MIN_BOX_PX） | face-dark-suppression | V-S03 | vision/server/S03-http-face-overlay.md | 串口 | 部分实现 |
| F0525 | MCP `self.face.set_mode` | face-mcp-set-mode | V-S07 | vision/server/S07-face-control-mcp.md | WS-MCP | 已实现 |
| F0526 | MCP `self.face.manage`（clear/delete/rename/merge/refresh_immich） | face-mcp-manage | V-S07 | vision/server/S07-face-control-mcp.md | WS-MCP | 已实现 |
| F0527 | MCP `self.face.list`（可选 include_live） | face-mcp-list | V-S07 | vision/server/S07-face-control-mcp.md | WS-MCP | 已实现 |
| F0528 | FaceControl 统一 API（检测/识别分拆 + CRUD + alias + registry） | face-control-unified | V-S07 | vision/server/S07-face-control-mcp.md | WS-MCP | 已实现 |
| F0529 | meta 字段 `immich_asset_id`/`canonical_id`/`name_pending` | face-meta-fields | V-S07 | vision/server/S07-face-control-mcp.md | 串口 | 已实现 |
| F0530 | 识别降频 sticky（IoU 沿用上帧 ID） | face-sticky-prev-id | V-S04 | vision/server/S04-local-face-numeric-id.md | 串口 | 已实现 |
| F0531 | 独立 `dog_face_ai` 任务、可关检测即停识别 | face-task-isolation | V-S04 | vision/server/S04-local-face-numeric-id.md | HTTP | 已实现 |
| F0532 | MCP 注册受 `DEEP_DOG_FACE_AI_ENABLE` 门控 | face-mcp-registration | V-S07 | vision/server/S07-face-control-mcp.md | WS-MCP | 已实现 |
| F0533 | `event/face/thumb`（planned） | face-thumb-event | V-S07 | mqtt/modules/04-face.md | MQTT | 待办 |
| F0534 | `detect_interval_ms` 运行时夹紧 [200,5000] | face-interval-clamp | V-S07 | vision/server/S07-face-control-mcp.md | 串口 | 已实现 |
| F0535 | facedb 写盘在 internal 栈（避免 cache assert） | face-facedb-internal-stack | V-S08 | vision/server/S08-rtsp-face-wdt-mitigation.md | 串口 | 已实现 |

---

## immich · Immich 真名（F0600–F0649）

| F-ID | 功能点 | slug | 路线图ID | 出处 | 通道 | 状态 |
|------|--------|------|----------|------|------|------|
| F0600 | 门控调用 Immich（无真名/sticky 帧） | immich-gating | V-S05 | vision/server/S05-immich-real-name.md | 串口 | 已实现 |
| F0601 | 异步 upload→轮询 people→绑定真名 | immich-upload-poll-bind | V-S05 | vision/server/S05-immich-real-name.md | 串口 | 已实现 |
| F0602 | 异步不阻塞检测/MJPEG/狗控 | immich-async-nonblocking | V-S05 | vision/server/S05-immich-real-name.md | 串口 | 已实现 |
| F0603 | 失败降级为数字 ID | immich-fallback-numeric | V-S05 | vision/server/S05-immich-real-name.md | HTTP | 已实现 |
| F0604 | 可观测日志（upload/poll/result/keep/delete） | immich-observability-logs | V-S05 | vision/server/S05-immich-real-name.md | 串口 | 已实现 |
| F0605 | 不主动 `PUT /jobs` | immich-no-job-trigger | V-S05 | vision/server/S05-immich-real-name.md | 串口 | 已实现 |
| F0606 | 临时 asset 默认保留、可配删除 | immich-temp-asset-policy | V-S05 | vision/server/S05-immich-real-name.md | HTTP | 已实现 |
| F0607 | `POST /api/immich_config` | immich-config-api | V-S05 | vision/server/S05-immich-real-name.md | HTTP | 已实现 |
| F0608 | `GET /api/immich_status` | immich-status-api | V-S05 | vision/server/S05-immich-real-name.md | HTTP | 已实现 |
| F0609 | `POST /api/face_refresh_name` | immich-face-refresh-name | V-S05 | vision/server/S05-immich-real-name.md | HTTP | 已实现 |
| F0610 | deferred poll（`name_pending`，默认 300s） | immich-deferred-poll | V-S05 | vision/server/S05-immich-real-name.md | 串口 | 已实现 |
| F0611 | 1080 时间轴 ISO8601 UTC | immich-timeline-iso8601 | V-S05 | vision/server/S05-immich-real-name.md | 串口 | 已实现 |
| F0612 | GPS lat/lon → `PUT /assets/{id}` | immich-gps-put | V-S05 | vision/server/S05-immich-real-name.md | MQTT | 已实现 |
| F0613 | 上传前 `GET /server/ping` 健康检查 | immich-ping | V-S05 | vision/server/S05-immich-real-name.md | 串口 | 已实现 |
| F0614 | `status/face/immich` retain（configured/server_ok/key_len） | immich-mqtt-status | V-S05 | mqtt/modules/04-face-immich-mqtt.md | MQTT | 已实现 |
| F0615 | `cmd/face action=set_immich_config` | immich-mqtt-set-config | V-S05 | mqtt/modules/04-face-immich-mqtt.md | MQTT | 已实现 |
| F0616 | 无 action 的稀疏配置更新 | immich-mqtt-sparse | V-S05 | mqtt/modules/04-face-immich-mqtt.md | MQTT | 已实现 |
| F0617 | `action=ping_immich` 探活 | immich-mqtt-ping | V-S05 | mqtt/modules/04-face-immich-mqtt.md | MQTT | 已实现 |
| F0618 | 周期性 Immich 探活（每 120 poll） | immich-periodic-ping | V-S05 | mqtt/modules/04-face-immich-mqtt.md | 串口 | 已实现 |
| F0619 | key 不明文上行（仅 key_len） | immich-no-key-echo | V-S05 | mqtt/modules/04-face-immich-mqtt.md | MQTT | 已实现 |
| F0620 | Immich crop 短边 ≥320 | immich-min-crop | V-S06 | vision/server/S06-higher-resolution.md | 串口 | 已实现 |
| F0621 | Immich crop 上限（实际 400） | immich-max-crop | V-S06 | vision/server/S06-higher-resolution.md | 串口 | 部分实现 |

---

## greet · 主动招呼 / 人物（F0650–F0679）

| F-ID | 功能点 | slug | 路线图ID | 出处 | 通道 | 状态 |
|------|--------|------|----------|------|------|------|
| F0650 | 识别已命名熟人 → `WakeWordInvoke("face_greet:<name>")` | greet-wake | V-P02 | vision/product/P02-proactive-face-greet.md | 串口 | 已实现 |
| F0651 | 间隔门控 `greet_gap_sec`（默认 1800） | greet-gap-gate | V-P02 | vision/product/P02-proactive-face-greet.md | MQTT | 已实现 |
| F0652 | 仅「已确认真名」才招呼 | greet-confirmed-name-only | V-P02 | vision/product/P02-proactive-face-greet.md | 串口 | 已实现 |
| F0653 | `status/device.current_speaker` | greet-current-speaker | V-P02 | vision/product/P02-proactive-face-greet.md | MQTT | 已实现 |
| F0654 | `status/person/active` retain（local_id/display_name） | greet-person-active | V-P02 | mqtt/modules/13-person.md | MQTT | 已实现 |
| F0655 | MCP `self.face.get_identity` | greet-mcp-get-identity | V-P02 | vision/product/P02-proactive-face-greet.md | WS-MCP | 已实现 |
| F0656 | MCP `self.face.simulate_greet` | greet-mcp-simulate | V-P02 | vision/product/P02-proactive-face-greet.md | WS-MCP | 已实现 |
| F0657 | MCP `self.face.clear_speaker` | greet-mcp-clear-speaker | V-P02 | vision/product/P02-proactive-face-greet.md | WS-MCP | 已实现 |
| F0658 | MCP `self.face.set_greet_config` | greet-mcp-set-config | V-P02 | vision/product/P02-proactive-face-greet.md | WS-MCP | 已实现 |
| F0659 | MQTT `cmd/face simulate_greet` | greet-mqtt-simulate | V-P02 | mqtt/modules/04-face.md | MQTT | 已实现 |
| F0660 | MQTT `cmd/face clear_speaker` | greet-mqtt-clear-speaker | V-P02 | mqtt/modules/04-face.md | MQTT | 已实现 |
| F0661 | MQTT `cmd/face set_greet_config` | greet-mqtt-set-config | V-P02 | mqtt/modules/04-face.md | MQTT | 已实现 |
| F0662 | `cmd/face` 顶层 `greet_enabled`/`greet_gap_sec` | greet-mqtt-top-level | V-P02 | mqtt/modules/04-face.md | MQTT | 已实现 |
| F0663 | enroll 成功写 `last_seen_at` | greet-enroll-last-seen | V-P02 | vision/product/P02-proactive-face-greet.md | 串口 | 已实现 |
| F0664 | init 补 orphan 槽不写 `last_seen_at` | greet-orphan-no-last-seen | V-P02 | vision/product/P02-proactive-face-greet.md | 串口 | 已实现 |

---

## track · 人脸跟踪（F0680–F0699）

| F-ID | 功能点 | slug | 路线图ID | 出处 | 通道 | 状态 |
|------|--------|------|----------|------|------|------|
| F0680 | `cmd/track enable/disable` | track-cmd-enable | M01 | mqtt/modules/05-track.md | MQTT | 已实现 |
| F0681 | `status/track`（enabled/following/target/actuator/error） | track-status | M01 | mqtt/modules/05-track.md | MQTT | 已实现 |
| F0682 | 检测关/无目标 → `following=false`（face_off/no_target） | track-errors | M01 | mqtt/modules/05-track.md | MQTT | 已实现 |
| F0683 | actuator 恒 none（无真实 pan/tilt） | track-actuator-none | M01 | mqtt/modules/05-track.md | MQTT | 已实现 |

---

## touch · 触摸按键（F0700–F0749）

| F-ID | 功能点 | slug | 路线图ID | 出处 | 通道 | 状态 |
|------|--------|------|----------|------|------|------|
| F0700 | 三键电容采样 + 去抖（GPIO1/2/3，50ms） | touch-sample | M01 | mqtt/modules/06-touch.md | 串口 | 已实现 |
| F0701 | 事件 press/release/long_press/short_press/double_click | touch-events | M01 | mqtt/modules/06-touch.md | MQTT | 已实现 |
| F0702 | `pressed_mask`（bit0/1/2） | touch-pressed-mask | M01 | mqtt/modules/06-touch.md | MQTT | 已实现 |
| F0703 | 阈值上报（ratio/offset/min，始终上报） | touch-thresholds-report | M01 | mqtt/modules/06-touch.md | MQTT | 已实现 |
| F0704 | `set_thresholds`（NVS 持久化） | touch-set-thresholds | M01 | mqtt/modules/06-touch.md | MQTT | 已实现 |
| F0705 | `calibrate`（button_id/samples，250ms 刷新） | touch-calibrate | M01 | mqtt/modules/06-touch.md | MQTT | 已实现 |
| F0706 | `reset_thresholds` | touch-reset-thresholds | M01 | mqtt/modules/06-touch.md | MQTT | 已实现 |
| F0707 | `cancel_calibrate` | touch-cancel-calibrate | M01 | mqtt/modules/06-touch.md | MQTT | 已实现 |
| F0708 | `debug:true` 强制 status 带 `debug[]` | touch-debug | M01 | mqtt/modules/06-touch.md | MQTT | 已实现 |
| F0709 | 跨键组合识别（chord/hold+tap） | touch-combo | M01 | touch_btn/COMBO_EVAL.md | MQTT | 已实现 |
| F0710 | 配对手势 → `event/pairing/request` | touch-pairing-gesture | M01 | mqtt/modules/00-pairing.md | MQTT | 已实现 |
| F0711 | App：log（全事件打印） | touch-app-log | M01 | touch_btn/README.md | 串口 | 已实现 |
| F0712 | App：dog（键1 long=init、2+3 停等） | touch-app-dog | M01 | touch_btn/README.md | 串口 | 已实现 |
| F0713 | App：servo（占位日志） | touch-app-servo | — | touch_btn/README.md | 串口 | 占位 |
| F0714 | `status/touch` retain（pressed_mask/buttons/thresholds/calib） | touch-status | M01 | mqtt/modules/06-touch.md | MQTT | 已实现 |

---

## handle · 手柄输入（F0750–F0799）

| F-ID | 功能点 | slug | 路线图ID | 出处 | 通道 | 状态 |
|------|--------|------|----------|------|------|------|
| F0750 | wifi 源 `cmd/handle/input` 解析（axes/buttons/source） | handle-input-wifi | I-HANDLE | mqtt/modules/11-handle.md | MQTT | 已实现 |
| F0751 | 输入超时清零（1000ms） | handle-input-timeout | I-HANDLE | mqtt/modules/11-handle.md | MQTT | 已实现 |
| F0752 | `status/handle`（merged axes/buttons/source，节流） | handle-status | I-HANDLE | mqtt/modules/11-handle.md | MQTT | 已实现 |
| F0753 | 触控板 XY + 双点 contacts 透传 | handle-touchpad-xy | I06 | input/I06-touchpad-xy.md | MQTT | 已实现 |
| F0754 | 手柄 motion（gyro dps / accel g）透传 | handle-motion | I07 | input/I07-motion-gyro.md | MQTT | 已实现 |
| F0755 | 板载 BT 源（Bluepad32+BTstack Xbox，source=bt） | handle-bt-source | I02 | input/I02-source-bluepad32-xbox.md | 串口 | 部分实现 |
| F0756 | BT 反向震动 `action:rumble` | handle-rumble-bt | I02 | mqtt/modules/11-handle.md | MQTT | 部分实现 |
| F0757 | PC 桥输出 `action:output`（设备侧发布） | handle-output | I09 | input/I09-ds4-output-feedback.md | MQTT | 已实现 |
| F0758 | `enable`/`disable` 门控 App | handle-enable-disable | I-HANDLE | mqtt/modules/11-handle.md | MQTT | 已实现 |
| F0759 | keymap 离散 press/hold（set/get/reset） | handle-keymap-discrete | I08a | input/I08a-keymap-mqtt-contract-draft.md | MQTT | 已实现 |
| F0760 | 运行时轴映射 `axis_bindings`（六轴） | handle-keymap-axis | I08b | input/I08b-axis-mapping.md | MQTT | 已实现 |
| F0761 | profile 切换（led_demo/gimbal/motor/dog/off） | handle-profile | I08a | input/I08a-keymap-mqtt-contract-draft.md | MQTT | 已实现 |
| F0762 | `status/handle/keymap` retain（schema_ver/bindings/catalog） | handle-keymap-retain | I08a | mqtt/modules/11-handle.md | MQTT | 已实现 |
| F0763 | keymap catalog 按 profile 过滤 | handle-keymap-catalog | I08a | input/I08a-keymap-mqtt-contract-draft.md | MQTT | 已实现 |
| F0764 | App：log（联调打印） | handle-app-log | I04 | input/I04-apps-mapping.md | 串口 | 已实现 |
| F0765 | App：dog（ly 前进后退/start init/b 停） | handle-app-dog | I04 | input/I04-apps-mapping.md | 串口 | 已实现 |
| F0766 | App：servo（占位） | handle-app-servo | I04 | input/I04-apps-mapping.md | 串口 | 占位 |
| F0767 | 双源 Hub 快照合并（后到覆盖） | handle-hub-merge | I01 | input/I01-architecture.md | 内部 | 已实现 |
| F0768 | `cmd/handle pair`（板载 BT 配对） | handle-pair-cmd | I02 | mqtt/modules/11-handle.md | MQTT | 部分实现 |
| F0769 | PS4/Xbox 归一化映射（PC 桥） | handle-normalize-map | I03 | input/I03-source-pc-mqtt-bridge.md | 串口 | 已实现 |
| F0770 | handle 数据点注册 | handle-datapoints | DP-01 | mqtt/modules/11-handle.md | MQTT | 已实现 |

---

## led · WS2812 灯带（F0800–F0849）

| F-ID | 功能点 | slug | 路线图ID | 出处 | 通道 | 状态 |
|------|--------|------|----------|------|------|------|
| F0800 | 灯带初始化（WS2812，24 珠，DIN=gpio_a） | led-init | M01 | mqtt/modules/08-led.md | 串口 | 已实现 |
| F0801 | `cmd/led` 稀疏控制（mode/brightness/rgb/interval…） | led-cmd | M01 | mqtt/modules/08-led.md | MQTT | 已实现 |
| F0802 | 模式 0–4（off/static/blink/breathe/scroll） | led-modes | M01 | mqtt/modules/08-led.md | MQTT | 已实现 |
| F0803 | mode=5 交还系统应用绑定 | led-system-bind | M01 | mqtt/modules/08-led.md | MQTT | 已实现 |
| F0804 | `status/led` retain（含 led_count/ok） | led-status | M01 | mqtt/modules/08-led.md | MQTT | 已实现 |
| F0805 | MCP `self.led_strip.*` | led-mcp | M02 | mqtt/modules/08-led.md | WS-MCP | 已实现 |
| F0806 | MQTT 与 MCP 共用同一控制层快照 | led-unified-control | M01 | mqtt/modules/08-led.md | 内部 | 已实现 |
| F0807 | 状态变化回调 → 同步 status | led-notify-status | M01 | mqtt/modules/08-led.md | MQTT | 已实现 |
| F0808 | led 数据点注册 | led-datapoints | DP-01 | mqtt/modules/08-led.md | MQTT | 已实现 |
| F0809 | 硬件前置 `EXT_PIN_MODE=LED` + `LED_ENABLE` | led-capability | CFG-02 | mqtt/modules/08-led.md | MQTT | 已实现 |

---

## gimbal · 云台（F0850–F0899）

| F-ID | 功能点 | slug | 路线图ID | 出处 | 通道 | 状态 |
|------|--------|------|----------|------|------|------|
| F0850 | 双轴 pan/tilt 初始化 | gimbal-init | V-C04 | mqtt/modules/09-gimbal.md | 串口 | 已实现 |
| F0851 | 绝对角 `mode=absolute` | gimbal-absolute | V-C04 | mqtt/modules/09-gimbal.md | MQTT | 已实现 |
| F0852 | 相对角 `mode=relative` | gimbal-relative | V-C04 | mqtt/modules/09-gimbal.md | MQTT | 已实现 |
| F0853 | 单步 nudge（left/right/up/down） | gimbal-nudge | V-C04 | mqtt/modules/09-gimbal.md | MQTT | 已实现 |
| F0854 | 连动 jog_start/jog_stop | gimbal-jog | V-C04 | mqtt/modules/09-gimbal.md | MQTT | 已实现 |
| F0855 | 调速 pan/tilt speed ± 档 | gimbal-speed-step | V-C04 | mqtt/modules/09-gimbal.md | MQTT | 已实现 |
| F0856 | `set_speed`（°/s） | gimbal-set-speed | V-C04 | mqtt/modules/09-gimbal.md | MQTT | 已实现 |
| F0857 | `stop`（角度保持） | gimbal-stop | V-C04 | mqtt/modules/09-gimbal.md | MQTT | 已实现 |
| F0858 | `home` 回中复位 | gimbal-home | V-C04 | mqtt/modules/09-gimbal.md | MQTT | 已实现 |
| F0859 | 摇杆速率 pan_rate/tilt_rate（轴映射） | gimbal-axis-rate | I08b | input/I08b-axis-mapping.md | 串口 | 已实现 |
| F0860 | `status/gimbal` retain（pan/tilt/速度/限位/moving/ready） | gimbal-status | V-C04 | mqtt/modules/09-gimbal.md | MQTT | 已实现 |
| F0861 | 非法 JSON 不崩溃 | gimbal-invalid-json | V-C04 | mqtt/modules/09-gimbal.md | MQTT | 已实现 |
| F0862 | gimbal 数据点注册 | gimbal-datapoints | DP-01 | mqtt/modules/09-gimbal.md | MQTT | 已实现 |

---

## servo · 裸舵机（F0900–F0949）

| F-ID | 功能点 | slug | 路线图ID | 出处 | 通道 | 状态 |
|------|--------|------|----------|------|------|------|
| F0900 | 两路舵机初始化（GPIO38/48，MCPWM 50Hz） | servo-init | M01 | mqtt/modules/10-servo.md | 串口 | 已实现 |
| F0901 | 设角度（index/angle，钳位） | servo-set-angle | M01 | mqtt/modules/10-servo.md | MQTT | 已实现 |
| F0902 | `duration_ms` 到位插值（0=立即） | servo-duration | M01 | mqtt/modules/10-servo.md | MQTT | 已实现 |
| F0903 | attach/detach + type 切换（90/180/270/360） | servo-attach-type | M01 | mqtt/modules/10-servo.md | MQTT | 已实现 |
| F0904 | `status/servo` retain（angle/target/attached/moving/type） | servo-status | M01 | mqtt/modules/10-servo.md | MQTT | 已实现 |
| F0905 | MCP `self.servo.*`（set_angle/set_type/attach/detach/get_status） | servo-mcp | M02 | mqtt/modules/10-servo.md | WS-MCP | 已实现 |
| F0906 | 非法 index/type 拒绝 | servo-bad-args | M01 | mqtt/modules/10-servo.md | MQTT | 已实现 |
| F0907 | servo 数据点注册 | servo-datapoints | DP-01 | mqtt/modules/10-servo.md | MQTT | 已实现 |

---

## imu · IMU 六轴 / 动作开关（F0950–F0999）

| F-ID | 功能点 | slug | 路线图ID | 出处 | 通道 | 状态 |
|------|--------|------|----------|------|------|------|
| F0950 | BMI270 六轴读取（accel m/s² / gyro dps） | imu-read | D9 | mqtt/modules/03-imu.md | MQTT | 已实现 |
| F0951 | 姿态估计 pitch/roll | imu-pose | D9 | mqtt/modules/03-imu.md | MQTT | 已实现 |
| F0952 | 本地 100Hz 采集 + 12 路边沿识别 | imu-local-detect | D9 | mqtt/modules/03-imu.md | 串口 | 已实现 |
| F0953 | 12 路开关边沿计数上报（~10Hz） | imu-switch-counts | D9 | mqtt/modules/03-imu.md | MQTT | 已实现 |
| F0954 | 阈值/cooldown 参数（rot 75°/trans 6.5） | imu-thresholds | D9 | mqtt/modules/03-imu.md | 内部 | 已实现 |
| F0955 | 无下行 `cmd/imu`（v0.1） | imu-no-cmd | D9 | mqtt/modules/03-imu.md | 内部 | 已实现 |
| F0956 | `status/imu` 约 10Hz | imu-status | D9 | mqtt/modules/03-imu.md | MQTT | 已实现 |
| F0957 | 无芯片/不可读时 `ok=false` 且 switches 全 0 | imu-unavailable | D9 | mqtt/modules/03-imu.md | MQTT | 已实现 |
| F0958 | imu 数据点注册 | imu-datapoints | DP-01 | mqtt/modules/03-imu.md | MQTT | 已实现 |

---

## can · CAN 总线（F1000–F1049）

| F-ID | 功能点 | slug | 路线图ID | 出处 | 通道 | 状态 |
|------|--------|------|----------|------|------|------|
| F1000 | TWAI 初始化（GPIO38 TX/48 RX，1Mbps） | can-init-twai | MOT-01 | motor/01-can-hw-profile.md | 串口 | 已实现 |
| F1001 | 原始帧注入 `cmd/can/tx`（仅 allow_tx） | can-raw-tx | MOT-02 | mqtt/modules/12-can.md | MQTT | 已实现 |
| F1002 | 总线帧监视 tunnel（节流/批/扩展帧过滤） | can-tunnel | MOT-02 | mqtt/modules/12-can.md | MQTT | 已实现 |
| F1003 | `status/can` retain（ok/tunnel/allow_tx/错误计数） | can-status | MOT-02 | mqtt/modules/12-can.md | MQTT | 已实现 |
| F1004 | `allow_tx` 默认关闭门控 | can-allow-tx-gate | MOT-02 | motor/VERIFY.md | MQTT | 已实现 |
| F1005 | `status/can/frames` 打包帧（节流） | can-frames | MOT-02 | mqtt/modules/12-can.md | MQTT | 已实现 |
| F1006 | 帧监听 Hub（供业务订阅） | can-frame-hub | MOT-01 | can/README.md | 内部 | 已实现 |
| F1007 | can 数据点注册（tunnel/allow_tx/dropped） | can-datapoints | DP-01 | mqtt/modules/12-can.md | MQTT | 已实现 |
| F1008 | 引脚 GPIO38/48 上报 | can-pins | MOT-01 | motor/01-can-hw-profile.md | MQTT | 已实现 |

---

## motor · 单电机 / 示教（F1050–F1149）

| F-ID | 功能点 | slug | 路线图ID | 出处 | 通道 | 状态 |
|------|--------|------|----------|------|------|------|
| F1050 | 电机使能/失能/reset | motor-enable | MOT-03 | motor/03-motor-mqtt.md | MQTT | 已实现 |
| F1051 | 位置点动（`position_rad`，钳位 ±12.57） | motor-position | MOT-03 | motor/03-motor-mqtt.md | MQTT | 已实现 |
| F1052 | 速度模式（`speed_rad_s`/`speed_limit`） | motor-speed | MOT-03 | motor/03-motor-mqtt.md | MQTT | 已实现 |
| F1053 | 电流环目标（`iq_ref`） | motor-iq-ref | MOT-03 | motor/03-motor-mqtt.md | MQTT | 已实现 |
| F1054 | MIT 运控（pos/vel/kp/kd/tau_ff） | motor-mit | MOT-03 | motor/03-motor-mqtt.md | MQTT | 已实现 |
| F1055 | 反馈回读（角/速/矩/温/故障/mode） | motor-feedback | MOT-03 | motor/03-motor-mqtt.md | MQTT | 已实现 |
| F1056 | 总线扫描发现 `cmd/motor/scan` → `event/motor/scan` | motor-scan | MOT-01 | motor/06-bus-scan-get-device-id.md | MQTT | 已实现 |
| F1057 | 读取 device_id / MCU UID | motor-device-id | MOT-01 | motor/06-bus-scan-get-device-id.md | MQTT | 已实现 |
| F1058 | 读取软件版本 | motor-version | MOT-03 | motor/03-motor-mqtt.md | MQTT | 已实现 |
| F1059 | 主动上报开关 T24（10ms 周期） | motor-active-report | MOT-01 | motor/07-active-report-frame.md | 串口 | 已实现 |
| F1060 | 异步初始化状态机（none/initializing/ready/failed） | motor-async-init | MOT-01 | motor/09-async-init.md | MQTT | 已实现 |
| F1061 | 指令去重缓存（跳过相同 CAN） | motor-cmd-cache | MOT-03 | motor/README.md | 内部 | 已实现 |
| F1062 | 堵转/碰撞 latch 与最大扭矩观测 | motor-collision | MOT-03 | mqtt/modules/14-motor.md | MQTT | 已实现 |
| F1063 | 电机 MCP 工具（`self.motor.*`/`self.can.*`） | motor-mcp | MOT-05 | motor/10-motor-mcp-bridge.md | WS-MCP | 已实现 |
| F1064 | 正弦信号激励 | motor-sin | MOT-05 | motor/10-motor-mcp-bridge.md | WS-MCP | 已实现 |
| F1065 | 角度 LED 指示 / 呼吸灯（电机侧） | motor-led-angle | MOT-03 | motor/README.md | 串口 | 已实现 |
| F1066 | 录制 v2（t,pos,vel）+ EPScan 0x7026 | motor-teach-record | MOT-11 | motor/teaching/12-record-v2.md | MQTT | 已实现 |
| F1067 | 录制停止 + MQTT 快照 | motor-teach-stop | MOT-11 | motor/teaching/12-record-v2.md | MQTT | 已实现 |
| F1068 | MIT 三阶段播放（blend + 真实时间轴 × time_scale） | motor-teach-play | MOT-11 | motor/teaching/11-playback-mit.md | MQTT | 已实现 |
| F1069 | 多轴（6 轴）并行 record/play | motor-teach-multi | MOT-13 | motor/teaching/13-multi-axis.md | MQTT | 已实现 |
| F1070 | 录制状态摘要 `status/motor/teaching` | motor-teach-status | MOT-11 | motor/teaching/12-record-v2.md | MQTT | 已实现 |
| F1071 | `status/motor` retain（motors[] 富反馈，20ms 节流+1s 心跳） | motor-status | MOT-03 | mqtt/modules/14-motor.md | MQTT | 已实现 |
| F1072 | `status/motor/tools` retain（工具目录） | motor-tools | MOT-05 | motor/10-motor-mcp-bridge.md | MQTT | 已实现 |
| F1073 | `event/motor/mcp_result` | motor-mcp-result | M02 | mqtt/M02-mcp-call-control-plane.md | MQTT | 已实现 |
| F1074 | `cmd/motor mcp_call` 透传（白名单） | motor-mcp-call | M02 | mqtt/M02-mcp-call-control-plane.md | MQTT | 已实现 |
| F1075 | MQTT 白名单外工具拒绝 | motor-mcp-call-whitelist | M02 | mqtt/M02-mcp-call-control-plane.md | MQTT | 已实现 |
| F1076 | 手柄 `profile=motor`（`rx→motor.pos_norm`） | motor-handle-bind | MOT-04 | motor/04-handle-motor-catalog.md | 串口 | 已实现 |
| F1077 | motor 数据点懒注册 | motor-datapoints | DP-01 | mqtt/modules/14-motor.md | MQTT | 已实现 |
| F1078 | `cmd/motor/report_start`/`report_stop` MQTT 主题（**固件未实现**；Web 端已发但设备无响应） | motor-report-topic | MOT-01 | motor/07-active-report-frame.md | MQTT | 待办 |

---

## dog · 四足运控（F1150–F1249）

| F-ID | 功能点 | slug | 路线图ID | 出处 | 通道 | 状态 |
|------|--------|------|----------|------|------|------|
| F1150 | 整机初始化（12 电机使能） | dog-init | D1 | dog/DEVELOPMENT_PLAN.md | WS-MCP | 已实现 |
| F1151 | 站立 | dog-stand | D1 | dog/DEVELOPMENT_PLAN.md | WS-MCP | 已实现 |
| F1152 | 卧倒 | dog-liedown | D1 | dog/DEVELOPMENT_PLAN.md | WS-MCP | 已实现 |
| F1153 | 前进一步（Trot 对角步态） | dog-forward | D3 | dog/DEVELOPMENT_PLAN.md | WS-MCP | 已实现 |
| F1154 | 后退一步 | dog-back | D3 | dog/DEVELOPMENT_PLAN.md | WS-MCP | 已实现 |
| F1155 | 一大步（半周期多小步 + 间隔） | dog-bigstep | D3 | dog/DEVELOPMENT_PLAN.md | WS-MCP | 已实现 |
| F1156 | 持续前进/后退（后台任务循环） | dog-continuous | D3 | dog/DEVELOPMENT_PLAN.md | WS-MCP | 已实现 |
| F1157 | 步态切换（SyncAllLegs ↔ Trot） | dog-gait-switch | D3 | dog/README.md | 串口 | 已实现 |
| F1158 | 姿态状态机 | dog-state-machine | D1 | dog/DEVELOPMENT_PLAN.md | 串口 | 已实现 |
| F1159 | 静态姿势点（站立/趴下/左右倾/前后倾） | dog-static-poses | D8 | dog/README.md | WS-MCP | 已实现 |
| F1160 | 跳舞（站立→前进/后退大程序列） | dog-dance | D5 | dog/DEVELOPMENT_PLAN.md | WS-MCP | 已实现 |
| F1161 | 失能（4 腿电机 reset） | dog-disable | D1 | dog/DEVELOPMENT_PLAN.md | WS-MCP | 已实现 |
| F1162 | 运控模式 MIT（p/v/kp/kd/tau） | dog-mit-walk | D7 | dog/DEVELOPMENT_PLAN.md | 串口 | 已实现 |
| F1163 | 步态速度/步频运行时调节 | dog-speed-tune | D5 | dog/DEVELOPMENT_PLAN.md | WS-MCP | 已实现 |
| F1164 | 中文状态摘要（供语音反馈） | dog-chassis-status-string | D4 | dog/DEVELOPMENT_PLAN.md | WS-MCP | 已实现 |
| F1165 | MCP 工具（`self.dog.*`/`self.chassis.*`） | dog-mcp-tools | D2 | interface/05-mcp-tool-normalization.md | WS-MCP | 已实现 |
| F1166 | 触摸映射（键1 long=init、组合键等） | dog-touch-map | D2 | dog/DEVELOPMENT_PLAN.md | 串口 | 已实现 |
| F1167 | 轨迹规划库（点对点/样条/插值 API） | dog-trajectory-lib | D7 | trajectory/README.md | 串口 | 已实现 |
| F1168 | 一大步/跳舞接入 trajectory 插值 | dog-bigstep-interp | D7 | dog/DEVELOPMENT_PLAN.md | 串口 | 待办 |
| F1169 | 力矩（堵转）保护 | dog-torque-protect | D6 | dog/DEVELOPMENT_PLAN.md | 串口 | 待办 |
| F1170 | IMU 上报（BMI270 + `status/imu`） | dog-imu-report | D9 | dog/DEVELOPMENT_PLAN.md | MQTT | 部分实现 |
| F1171 | `cmd/dog` / `status/dog` MQTT 契约 | dog-mqtt | — | mqtt/modules/07-dog.md | MQTT | 待办 |
| F1172 | HTTP 狗控（`/api/cmd`、`/api/dog_status`） | dog-http | V-S01 | vision/server/S01-http-dog-motion.md | HTTP | 已实现 |
| F1173 | RK3588 CAN 协同 | dog-rk3588-can | D11 | dog/DEVELOPMENT_PLAN.md | 串口 | 待办 |
| F1174 | ROS 状态/控制（rviz） | dog-ros | D12 | dog/DEVELOPMENT_PLAN.md | 串口 | 待办 |
| F1175 | 左转/右转步态补齐 | dog-turn | D3 | dog/DEVELOPMENT_PLAN.md | 串口 | 待办 |
| F1176 | 单腿 MCP（`self.leg.*`） | dog-leg-mcp | D2 | leg/README.md | WS-MCP | 未见 |
| F1177 | 关节软件限位 + 机械限位夹紧 | dog-leg-limit | D1 | leg/README.md | 串口 | 已实现 |
| F1178 | `capabilities.dog` 标志位 | dog-capability | M01 | mqtt/modules/01-device.md | MQTT | 已实现 |
| F1179 | 机械臂占位（复用 CAN+MOTOR） | dog-arm-placeholder | — | FEATURE_FLAGS.md | 串口 | 占位 |
| F1180 | AD 采样占位 | dog-ad-placeholder | — | FEATURE_FLAGS.md | 串口 | 占位 |
| F1181 | RS485 占位 | dog-rs485-placeholder | — | FEATURE_FLAGS.md | 串口 | 占位 |
| F1182 | 纯 GPIO 扩展 IO 占位 | dog-ioext-placeholder | — | FEATURE_FLAGS.md | 串口 | 占位 |
| F1183 | UART 串口占位 | dog-uart-placeholder | — | FEATURE_FLAGS.md | 串口 | 占位 |

---

## dp · 数据点归一化（F1250–F1299）

| F-ID | 功能点 | slug | 路线图ID | 出处 | 通道 | 状态 |
|------|--------|------|----------|------|------|------|
| F1250 | 声明式数据点目录 + 全局注册表单例 | dp-registry-core | DP-00 | interface/04-datapoint-registry.md | 内部 | 已实现 |
| F1251 | 数据点模型（type/unit/range/access/channels/policy） | dp-model | DP-00 | interface/04-datapoint-registry.md | 内部 | 已实现 |
| F1252 | 归一化读 `ReadValue` | dp-read | DP-00 | interface/04-datapoint-registry.md | HTTP | 已实现 |
| F1253 | 归一化写 + 统一校验（权限/类型/枚举/范围） | dp-write-validate | DP-00 | interface/04-datapoint-registry.md | HTTP | 已实现 |
| F1254 | 通道① MQTT：`status/<module>` 序列化（retain） | dp-channel-mqtt | DP-00 | interface/04-datapoint-registry.md | MQTT | 已实现 |
| F1255 | 通道① MQTT：`cmd/<module>` 下行写入 | dp-channel-mqtt-cmd | DP-00 | interface/04-datapoint-registry.md | MQTT | 已实现 |
| F1256 | 通道② HTTP：`/api/datapoints/schema` | dp-http-schema | DP-02 | interface/04-datapoint-registry.md | HTTP | 已实现 |
| F1257 | 通道② HTTP：`/api/datapoints/module/<name>` | dp-http-module | DP-02 | interface/04-datapoint-registry.md | HTTP | 已实现 |
| F1258 | 通道② HTTP：`/api/datapoints/point/<id>` | dp-http-point | DP-02 | interface/04-datapoint-registry.md | HTTP | 已实现 |
| F1259 | 通道② HTTP：`/api/datapoints/set` | dp-http-set | DP-02 | interface/04-datapoint-registry.md | HTTP | 已实现 |
| F1260 | 通道② HTTP：`/api/datapoints/batch`、`batch_set` | dp-http-batch | DP-02 | interface/04-datapoint-registry.md | HTTP | 未见 |
| F1261 | 通道③ MCP：自动生成 `self.<module>.get_/set_<point>` | dp-channel-mcp | DP-02 | interface/04-datapoint-registry.md | WS-MCP | 已实现 |
| F1262 | 通道④ Telemetry：数值点自动汇入 `status/telemetry.points` | dp-channel-telemetry | DT-01 | interface/04-datapoint-registry.md | MQTT | 已实现 |
| F1263 | Schema 自动发现 `info/schema`（retain） | dp-info-schema | DP-00 | interface/04-datapoint-registry.md | MQTT | 已实现 |
| F1264 | 子实例机制（`servo.<i>`/`motor.<id>`）与懒注册 | dp-sub-instances | DP-00 | interface/04-datapoint-registry.md | MQTT | 已实现 |
| F1265 | MCP 工具归一化铁律（标量→数据点，动作→手写） | dp-mcp-normalization-rule | DP-02 | interface/05-mcp-tool-normalization.md | 内部 | 已实现 |
| F1266 | HTTP 端点复用 WS MCP 的 httpd | dp-http-reuse-wsmcp-httpd | DP-02 | ll/LL-004-httpd-and-datapoint-http.md | HTTP | 已实现 |
| F1267 | 旧 HTTP 端点兼容层 | dp-legacy-endpoint-compat | DP-02 | interface/04-datapoint-registry.md | HTTP | 部分实现 |
| F1268 | 发布 `status/telemetry`（points/ts） | telemetry-publish | DT-01 | mqtt/modules/15-telemetry.md | MQTT | 已实现 |
| F1269 | 统一骨架话题 `{prefix}/{device_id}/status/telemetry` | telemetry-unified-topic | DT-01 | mqtt/modules/15-telemetry.md | MQTT | 已实现 |
| F1270 | 未配置话题时不发布、无报错 | telemetry-disabled | DT-01 | mqtt/modules/15-telemetry.md | MQTT | 已实现 |
| F1271 | `points` 由数据点注册表自动汇入 | telemetry-datapoints | DT-01 | mqtt/modules/15-telemetry.md | MQTT | 已实现 |
| F1272 | 默认 6 点（heap/rssi/uptime…） | telemetry-default-points | DT-01 | mqtt/modules/15-telemetry.md | MQTT | 已实现 |
| F1273 | 上报节奏节流（默认约 15s） | telemetry-interval | DT-01 | mqtt/modules/15-telemetry.md | 串口 | 已实现 |
| F1274 | home/work_unit 从旧话题反推兜底 | telemetry-derive | DT-01 | mqtt/modules/15-telemetry.md | MQTT | 已实现 |

---

## mcp · 局域网 WS-MCP 桥（F1300–F1349）

| F-ID | 功能点 | slug | 路线图ID | 出处 | 通道 | 状态 |
|------|--------|------|----------|------|------|------|
| F1300 | WS 服务端启动 | mcp-server-start | M02 | ws-mcp/01-local-mcp-bridge.md | WS-MCP | 已实现 |
| F1301 | 开关默认值（`(MOTOR&&!DOG)||FACE_AI`） | mcp-enable-default | M02 | interface/02-ws-contract.md | 内部 | 已实现 |
| F1302 | 端点 `8080` + 路径 `/ws` | mcp-endpoint | M02 | interface/02-ws-contract.md | WS-MCP | 已实现 |
| F1303 | WS 握手 | mcp-handshake | M02 | ws-mcp/01-local-mcp-bridge.md | WS-MCP | 已实现 |
| F1304 | 直发 JSON-RPC 入站 | mcp-jsonrpc-direct | M02 | interface/02-ws-contract.md | WS-MCP | 已实现 |
| F1305 | 信封入站 `{"type":"mcp","payload":{...}}` | mcp-envelope | M02 | interface/02-ws-contract.md | WS-MCP | 已实现 |
| F1306 | 标准 MCP 方法 `initialize/tools/list/tools/call` | mcp-methods | M02 | interface/02-ws-contract.md | WS-MCP | 已实现 |
| F1307 | 单帧上限 4096 字节 | mcp-max-frame | M02 | interface/02-ws-contract.md | WS-MCP | 已实现 |
| F1308 | 响应广播到所有已连接客户端 | mcp-broadcast | M02 | interface/02-ws-contract.md | WS-MCP | 已实现 |
| F1309 | 客户端接入/断开（CLOSE 帧清理） | mcp-client-lifecycle | M02 | interface/02-ws-contract.md | WS-MCP | 已实现 |
| F1310 | 与内置 httpd 同端口编译期拦截 | mcp-port-conflict-guard | M02 | interface/02-ws-contract.md | 内部 | 已实现 |
| F1311 | 数据点自动 MCP 工具注册 | mcp-dp-tools | DP-02 | interface/04-datapoint-registry.md | WS-MCP | 已实现 |
| F1312 | WS 鉴权/TLS | mcp-auth-tls | — | interface/02-ws-contract.md | 内部 | 待办 |
| F1313 | WS 应用层心跳 ping/pong | mcp-heartbeat | — | interface/02-ws-contract.md | WS-MCP | 未见 |
| F1314 | 前端 deep-trace 设备页 WS 连接面板 PoC | mcp-frontend-poc | — | ws-mcp/01-local-mcp-bridge.md | 前端 | 待办 |
| F1315 | MQTT/MCP/域 API 三端一致（face set_mode 等） | mcp-3way-consistency | M02 | mqtt/M02-mcp-call-control-plane.md | MQTT | 已实现 |
| F1316 | led/servo/dog 的 `mcp_call` 模式 | mcp-domain-mcp-call | M02 | mqtt/M02-mcp-call-control-plane.md | MQTT | 待办 |

---

## http · 内置 HTTP server / HTTP 契约（F1350–F1399）

| F-ID | 功能点 | slug | 路线图ID | 出处 | 通道 | 状态 |
|------|--------|------|----------|------|------|------|
| F1350 | 内置 httpd 开关（默认 0） | http-server-enable | — | interface/03-http-api-contract.md | 内部 | 已实现 |
| F1351 | 监听端口 8080 | http-server-port | — | interface/03-http-api-contract.md | HTTP | 已实现 |
| F1352 | CORS 头 + 禁缓存 | http-server-cors | — | interface/03-http-api-contract.md | HTTP | 已实现 |
| F1353 | `GET /` 内置遥控页 | http-root-remote-page | V-S01 | vision/server/S01-http-dog-motion.md | HTTP | 已实现 |
| F1354 | `GET /api/status` 采集/推流状态 | http-api-status | V-S02 | interface/03-http-api-contract.md | HTTP | 已实现 |
| F1355 | `POST /api/cmd` 狗动作（init/forward/…/disable） | http-api-dog-cmd | V-S01 | vision/server/S01-http-dog-motion.md | HTTP | 已实现 |
| F1356 | `GET /api/dog_status`（motors[]/torque/has_fault） | http-api-dog-status | V-S01 | vision/server/S01-http-dog-motion.md | HTTP | 部分实现 |
| F1357 | `GET /api/face` 人脸检测快照 | http-api-face | V-S03 | vision/server/S03-http-face-overlay.md | HTTP | 已实现 |
| F1358 | `POST /api/face_enable` | http-api-face-enable | V-S03 | vision/server/S03-http-face-overlay.md | HTTP | 已实现 |
| F1359 | `POST /api/immich_config` | http-api-immich-config | V-S05 | vision/server/S05-immich-real-name.md | HTTP | 已实现 |
| F1360 | `GET /api/immich_status` | http-api-immich-status | V-S05 | vision/server/S05-immich-real-name.md | HTTP | 已实现 |
| F1361 | `POST /api/face_refresh_name` | http-api-face-refresh-name | V-S05 | vision/server/S05-immich-real-name.md | HTTP | 已实现 |
| F1362 | IP 事件回调打印访问 URL | http-log-access-urls | V-S01 | vision/server/S01-http-dog-motion.md | 串口 | 已实现 |
| F1363 | httpd 弃用计划登记 | http-server-deprecation | — | interface/03-http-api-contract.md | 内部 | 待办 |

---

## rtsp · RTSP / MediaMTX / 流媒体客户端（F1400–F1449）

| F-ID | 功能点 | slug | 路线图ID | 出处 | 通道 | 状态 |
|------|--------|------|----------|------|------|------|
| F1400 | 主机 `:8888` 返回 `Server: mediamtx`、外网 HLS 可达 | rtsp-infra-reachability | V-C01 | vision/client/C01-stream-server-verify.md | HTTP | 部分实现 |
| F1401 | PC RTMP 推 → 内网/外网 HLS ≥30s | rtsp-rtmp-push-hls | V-C01 | vision/client/C01-stream-server-verify.md | RTSP | 部分实现 |
| F1402 | RTSP+JPEG 推 → MediaMTX 转 HLS | rtsp-jpeg-push-hls | V-C01 | vision/client/C01-stream-server-verify.md | RTSP | 部分实现 |
| F1403 | 路径 `deep-dog/dev` 与 infra 一致 | rtsp-path-consistency | V-C01 | vision/infra.md | 内部 | 已实现 |
| F1404 | 设备 RTSP 推流（H.264 软编） | rtsp-h264-push | V-C02 | vision/client/C02-device-push-stream.md | RTSP | 已实现 |
| F1405 | Python 拉流落盘门禁脚本 | rtsp-pull-verify-script | V-C02 | vision/client/C02-device-push-stream.md | RTSP | 已实现 |
| F1406 | 当前 TCP interleaved 传输 | rtsp-tcp-interleaved | V-C05 | vision/client/C05-rtsp-udp-transport.md | RTSP | 已实现 |
| F1407 | RTP over UDP 传输 | rtsp-rtp-over-udp | V-C05 | vision/client/C05-rtsp-udp-transport.md | RTSP | 待办 |
| F1408 | UDP 开关宏 `DEEP_DOG_VISION_RTSP_TRANSPORT_UDP` | rtsp-udp-macro | V-C05 | vision/client/C05-rtsp-udp-transport.md | 内部 | 未见 |
| F1409 | RTCP 质量监控/上报 `status/stream` | rtsp-rtcp-monitor | V-C05 | vision/client/C05-rtsp-udp-transport.md | RTSP | 待办 |
| F1410 | VisionHub→FaceAI 零拷贝 POC | rtsp-zerocopy-poc | V-S08 | vision/server/S08-rtsp-face-wdt-mitigation.md | 内部 | 待办 |
| F1411 | S09 baseline 采集脚本 | rtsp-baseline-capture | V-S09 | vision/server/S09-internal-sram-optimization.md | 串口 | 部分实现 |
| F1412 | S09 MEM-01 启动错峰 | rtsp-mem01-stagger-start | V-S09 | vision/server/S09-internal-sram-optimization.md | 串口 | 待办 |
| F1413 | S09 MEM-02 取消 `immich_late` internal 任务 | rtsp-mem02-drop-immich-late | V-S09 | vision/server/S09-internal-sram-optimization.md | 串口 | 待办 |
| F1414 | S09 MEM-06 识别模型 lazy load | rtsp-mem06-lazy-recognizer | V-S09 | vision/server/S09-internal-sram-optimization.md | 串口 | 待办 |
| F1415 | S09 MEM-07 `face_boot` 改 PSRAM 栈 | rtsp-mem07-face-boot-psram | V-S09 | vision/server/S09-internal-sram-optimization.md | 串口 | 待办 |
| F1416 | Kiosk 轮播切换该人物相册 | rtsp-kiosk-album-carousel | V-P01 | vision/product/P01-kiosk-personalization.md | 前端 | 未见 |
| F1417 | 对话注入个人档案 / 默认人设回退 | rtsp-dialog-persona-injection | V-P01 | vision/product/P01-kiosk-personalization.md | 前端 | 未见 |

---

## 汇总

- 功能点总数：见 [TESTS.md](./TESTS.md) 顶部统计（脚本生成时同步更新）。
- 状态分布与测试结果以 `TESTS.md` 与 `../TEST_REPORT.md` 为准。
