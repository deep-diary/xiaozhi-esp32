# deep-dog 测试用例注册表（TESTS）

> **唯一真源**：本文件是 deep-dog「测试用例编号」的权威注册表。
> 编号规则见 [CONVENTIONS.md §2.1](../../CONVENTIONS.md)；功能点见 [FEATURES.md](./FEATURES.md)。
> 用例 ID 默认与所测 `F` 同号；多组用字母后缀（如 `TC0011b`）。
> **最近一次测试的失败根因与契约冲突见 [FINDINGS.md](./FINDINGS.md)。**
> **跨剖面验证**：默认 `can` 剖面 58 条可执行用例全 PASS；`led` / `pwm+gimbal` / `pwm+servo`
> 三个剖面下 `TC0804` / `TC0860` / `TC0904` 各自单独 PASS（见 FINDINGS §0.3）。

## 图例

- **方式**：`自动(MQTT)` · `自动(HTTP)` · `自动(WS-MCP)` · `自动(RTSP)` · `串口`（需人工读日志）· `人工`（物理/观感）
- **脚本**：`cases/<file>.py::<func>`；`—` 表示尚未实现自动化，只登记用例设计。
- 运行入口：`python scripts/deep_dog/tests/run.py`（详见 `scripts/deep_dog/tests/README.md`）。
- 结果：`PASS` · `FAIL` · `SKIP` · `BLOCKED` · `ERROR`，最新结果见 [TEST_REPORT.md](../TEST_REPORT.md)。

---

## 1. 已实现自动化用例（67 条）

### device（F0001–F0099）

| TC-ID | F-ID | 用例 | 预期 | 方式 | 脚本 |
|-------|------|------|------|------|------|
| TC0001 | F0001 | 上线发布 info/device retain | retain 含 firmware/board/ip/capabilities/ext_pins | 自动(MQTT) | tc_mqtt.py::tc_device_info |
| TC0002 | F0002 | status/device 心跳 | 含 online/uptime_s/free_heap | 自动(MQTT) | tc_mqtt.py::tc_device_heartbeat |
| TC0003 | F0003 | 内存分项（status/system） | 含 internal_free/psram_free | 自动(MQTT) | tc_mqtt.py::tc_device_mem |
| TC0004 | F0004 | health.ok / warn[] | 心跳载荷含布尔 health.ok | 自动(MQTT) | tc_mqtt.py::tc_device_health |
| TC0005 | F0005 | clock_synced | 心跳载荷含布尔 clock_synced | 自动(MQTT) | tc_mqtt.py::tc_device_clock |
| TC0006 | F0006 | tasks[] / task_count | 含数组 tasks 与整型 task_count | 自动(MQTT) | tc_mqtt.py::tc_device_tasks |
| TC0011 | F0011 | capabilities 能力位 | 非空对象 | 自动(MQTT) | tc_mqtt.py::tc_device_capabilities |
| TC0011b | F0011 | capabilities.ws_mcp 一致性 | 8080 可连时 ws_mcp 应为 true | 自动(MQTT) | tc_wsmcp.py::tc_capability_wsmcp_consistency |
| TC0012 | F0012 | system 数据点注册 | info/schema 含 system | 自动(MQTT) | tc_mqtt.py::tc_device_system_dp |
| TC0014 | F0014 | MCP self.board.get_ip | 返回 ip/connected | 自动(WS-MCP) | tc_wsmcp.py::tc_mcp_get_ip |
| TC0015 | F0015 | MCP self.board.diagnostics | 内存+任务栈快照 | 自动(WS-MCP) | tc_wsmcp.py::tc_mcp_diag |
| TC0016 | F0016 | topic 骨架 | `{root}/{id}/info/device` 收到消息 | 自动(MQTT) | tc_mqtt.py::tc_topic_skeleton |
| TC0017 | F0017 | device_id = MAC 紧凑 | 匹配 `^[0-9a-f]{12}$` | 自动(MQTT) | tc_mqtt.py::tc_device_id_mac |
| TC0022 | F0022 | info/schema retain | 非空 | 自动(MQTT) | tc_mqtt.py::tc_info_schema |
| TC0065 | F0065 | 非法 cmd/device 健壮性 | 不崩溃、心跳续报 | 自动(MQTT) | tc_mqtt.py::tc_device_no_cmd |

### pair / net / config

| TC-ID | F-ID | 用例 | 预期 | 方式 | 脚本 |
|-------|------|------|------|------|------|
| TC0100 | F0100 | status/pairing retain | 含 device_id/bound | 自动(MQTT) | tc_mqtt.py::tc_pair_status |
| TC0208 | F0208 | info/device 广播 ip | 合法 IPv4 | 自动(MQTT) | tc_mqtt.py::tc_net_ip |
| TC0306 | F0306 | cmd/config refresh | 收到并回 ack | 自动(MQTT) | tc_mqtt.py::tc_config_refresh |
| TC0307 | F0307 | cmd/config/ack 结果 | accepted / unsupported | 自动(MQTT) | tc_mqtt.py::tc_config_ack |
| TC0308 | F0307 | 未知 action | ack.result=unsupported | 自动(MQTT) | tc_mqtt.py::tc_config_unknown |

### stream / face / immich / greet / track

| TC-ID | F-ID | 用例 | 预期 | 方式 | 脚本 |
|-------|------|------|------|------|------|
| TC0401 | F0401 | cmd/stream stop | state=off | 自动(MQTT) | tc_mqtt.py::tc_stream_stop |
| TC0403 | F0403 | 非法 cmd/stream | error 非空且不崩溃 | 自动(MQTT) | tc_mqtt.py::tc_stream_errors |
| TC0404 | F0404 | status/stream retain | 含 state/mode | 自动(MQTT) | tc_mqtt.py::tc_stream_status |
| TC0508 | F0508 | cmd/face refresh_status | 立即回 status/face | 自动(MQTT) | tc_mqtt.py::tc_face_refresh |
| TC0511 | F0511 | status/face/registry retain | 含 count/entries | 自动(MQTT) | tc_mqtt.py::tc_face_registry |
| TC0525 | F0525 | MCP self.face.set_mode | 调用成功 | 自动(WS-MCP) | tc_wsmcp.py::tc_mcp_face_set_mode |
| TC0527 | F0527 | MCP self.face.list | 返回库列表 | 自动(WS-MCP) | tc_wsmcp.py::tc_mcp_face_list |
| TC0534 | F0534 | detect_interval_ms 夹紧 | ≤5000 且回显 | 自动(MQTT) | tc_mqtt.py::tc_face_interval |
| TC0614 | F0614 | status/face/immich retain | 含 configured | 自动(MQTT) | tc_mqtt.py::tc_immich_status |
| TC0619 | F0619 | Immich key 不明文 | 无 api_key 字段 | 自动(MQTT) | tc_mqtt.py::tc_immich_no_key |
| TC0653 | F0653 | current_speaker | 有说话人时下发 | 自动(MQTT) | tc_mqtt.py::tc_current_speaker |
| TC0654 | F0654 | status/person/active | 有活跃人物时下发 | 自动(MQTT) | tc_mqtt.py::tc_person_active |
| TC0655 | F0655 | MCP self.face.get_identity | 返回身份 | 自动(WS-MCP) | tc_wsmcp.py::tc_mcp_get_identity |
| TC0681 | F0681 | cmd/track disable | status/track.enabled=false | 自动(MQTT) | tc_mqtt.py::tc_track_status |

### touch / handle / led / gimbal / servo / imu

| TC-ID | F-ID | 用例 | 预期 | 方式 | 脚本 |
|-------|------|------|------|------|------|
| TC0703 | F0703 | 阈值始终上报 | status/touch.thresholds | 自动(MQTT) | tc_mqtt.py::tc_touch_thresholds |
| TC0714 | F0714 | status/touch retain | 含 pressed_mask/buttons | 自动(MQTT) | tc_mqtt.py::tc_touch_status |
| TC0750 | F0750 | cmd/handle/input 注入 | status/handle 回显轴值 | 自动(MQTT) | tc_mqtt.py::tc_handle_input |
| TC0758 | F0758 | enable/disable 门控 | 状态可续报 | 自动(MQTT) | tc_mqtt.py::tc_handle_enable |
| TC0762 | F0762 | status/handle/keymap retain | 含 schema_ver | 自动(MQTT) | tc_mqtt.py::tc_handle_keymap |
| TC0804 | F0804 | status/led retain | LED 模式生效时含 mode | 自动(MQTT) | tc_mqtt.py::tc_led_status |
| TC0860 | F0860 | status/gimbal retain | PWM 模式生效时含 pan/tilt | 自动(MQTT) | tc_mqtt.py::tc_gimbal_status |
| TC0904 | F0904 | status/servo retain | PWM 模式生效时含 servos[] | 自动(MQTT) | tc_mqtt.py::tc_servo_status |
| TC0953 | F0953 | imu switches 计数 | 心跳载荷含 switches | 自动(MQTT) | tc_mqtt.py::tc_imu_switches |
| TC0956 | F0956 | status/imu 上报 | 含 accel/gyro/ok | 自动(MQTT) | tc_mqtt.py::tc_imu_status |

### can / motor / dp / mcp / http / rtsp

| TC-ID | F-ID | 用例 | 预期 | 方式 | 脚本 |
|-------|------|------|------|------|------|
| TC1003 | F1003 | status/can retain | 含 ok/tunnel/allow_tx | 自动(MQTT) | tc_mqtt.py::tc_can_status |
| TC1004 | F1004 | allow_tx 默认关 | allow_tx=false | 自动(MQTT) | tc_mqtt.py::tc_can_allow_tx |
| TC1050 | F1050 | cmd/motor 使能/失能 | status/motor 更新 | 自动(MQTT) | tc_mqtt.py::tc_motor_enable |
| TC1071 | F1071 | status/motor retain | 含 motors[] | 自动(MQTT) | tc_mqtt.py::tc_motor_status |
| TC1072 | F1072 | status/motor/tools retain | 非空 | 自动(MQTT) | tc_mqtt.py::tc_motor_tools |
| TC1074 | F1074 | cmd/motor mcp_call | 回 event/motor/mcp_result | 自动(MQTT) | tc_mqtt.py::tc_motor_mcp_call |
| TC1256 | F1256 | GET /api/datapoints/schema | 200 JSON | 自动(HTTP) | tc_http.py::tc_dp_schema |
| TC1257 | F1257 | GET .../module/device | 200 | 自动(HTTP) | tc_http.py::tc_dp_module |
| TC1258 | F1258 | GET .../point/device.device_id | 200 且值=设备 ID | 自动(HTTP) | tc_http.py::tc_dp_point |
| TC1259 | F1259 | POST /api/datapoints/set | 只读点被拒 | 自动(HTTP) | tc_http.py::tc_dp_set |
| TC1263 | F1263 | 数据点 schema 自动发现 | info/schema 为 JSON | 自动(MQTT) | tc_mqtt.py::tc_dp_schema |
| TC1268 | F1268 | status/telemetry 上报 | 已配置时含 points | 自动(MQTT) | tc_mqtt.py::tc_telemetry |
| TC1302 | F1302 | WS-MCP 端点可连 | tools/list 返回数组 | 自动(WS-MCP) | tc_wsmcp.py::tc_mcp_endpoint |
| TC1306 | F1306 | tools/list 方法 | 工具含 name | 自动(WS-MCP) | tc_wsmcp.py::tc_mcp_methods |
| TC1311 | F1311 | 数据点自动 MCP 工具 | 按 schema 推导的自动工具（当前 75 个）**全量**出现；内存守卫拦截则 BLOCKED 附实测字节（F-12） | 自动(WS-MCP) | tc_wsmcp.py::tc_mcp_dp_tools |
| TC1315 | F1315 | MQTT/MCP 三端一致 | MCP 与 MQTT 都有 face set_mode | 自动(WS-MCP) | tc_wsmcp.py::tc_mcp_3way |
| TC1350 | F1350 | 内置 HTTP server | 开启时 /api/status=200 | 自动(HTTP) | tc_http.py::tc_builtin_http |
| TC1352 | F1352 | 数据点 REST CORS | `Access-Control-Allow-Origin: *` | 自动(HTTP) | tc_http.py::tc_cors |
| TC1353 | F1353 | 内置遥控页 GET / | 返回 HTML | 自动(HTTP) | tc_http.py::tc_root_page |
| TC1356 | F1356 | GET /api/dog_status | 200 | 自动(HTTP) | tc_http.py::tc_dog_status |
| TC1360 | F1360 | GET /api/immich_status | 200 | 自动(HTTP) | tc_http.py::tc_immich_status_http |
| TC1400 | F1400 | MediaMTX 主机可达 | Server: mediamtx | 自动(HTTP) | tc_rtsp.py::tc_infra |
| TC1404 | F1404 | 设备 RTSP 推流 | 进入推流态且目标端口可达 | 自动(RTSP) | tc_rtsp.py::tc_rtsp_push |

---

## 1.1 串口用例（tag=`serial`，10 条，需 `--serial-cases`）

> 打开 COM6 会**复位设备**（LL-006），故这些用例默认不参与全量跑；启用时 runner 会把它们**排到最后**。
> 观测的是**一次冷启动**的日志。

| TC-ID | F-ID | 用例 | 预期 | 方式 | 脚本 |
|-------|------|------|------|------|------|
| TC0007 | F0007 | 设钟后重发 device/face registry | 日志 `clock synced: republished …` | 串口 | tc_serial.py::tc_serial_clock_resync |
| TC0013 | F0013 | 内存报告 | 日志 `dog_mem_rpt: memory report [post_activation]` + 任务 hwm 明细 | 串口 | tc_serial.py::tc_serial_mem_report |
| TC0019 | F0019 | MQTT 前缀根 | 日志 `dog_mqtt: start prefix=<root>/<id>/` 与配置一致 | 串口 | tc_serial.py::tc_serial_topic_root |
| TC0200 | F0200 | SNTP 启动 | 日志 `dog_sntp: SNTP init server=…` | 串口 | tc_serial.py::tc_serial_sntp_init |
| TC0204 | F0204 | SNTP 主/备服务器 | 日志含 `server=` 与 `backup=` | 串口 | tc_serial.py::tc_serial_sntp_servers |
| TC0206 | F0206 | STA DHCP 获取 IP | 日志 `WifiStation: Got IP: <ipv4>` | 串口 | tc_serial.py::tc_serial_dhcp |
| TC0320 | F0320 | ext_pins 启动目标解析 | 日志 `ext_pins mode=<m> (default=…)`，且 m ∈ supported_modes | 串口 | tc_serial.py::tc_serial_extpin |
| TC0417 | F0417 | RTSP 推流编码分辨率 | 日志 `H264 push ok … capture=WxH encode=wxh`，且 encode ≤ capture 且 ∈ 受支持尺寸 | 串口 | tc_serial.py::tc_serial_rtsp_encode |
| TC0952 | F0952 | 本地 100Hz 采集 + 12 路边沿 | 日志 `dog_mqtt_imu: switches rot=[…] trans=[…]` | 串口 | tc_serial.py::tc_serial_imu_detect |
| TC1000 | F1000 | TWAI 初始化 | 日志 `CAN init ok, TX=38 RX=48, queue tx=.. rx=..` | 串口 | tc_serial.py::tc_serial_can_init |

运行：`python scripts/deep_dog/tests/run.py --serial-cases`（或 `--tag serial`）。

## 1.2 补充命令类用例（27 条，`cases/tc_modules.py`）

> 覆盖「下发命令 → 观察 status 变化」的**无需额外硬件**项；用例尽量**自恢复**（用完把设备状态改回去）。

| TC-ID | F-ID | 用例 | 预期 | 方式 | 脚本 |
|-------|------|------|------|------|------|
| TC0680 | F0680 | cmd/track enable/disable | enable→enabled=true；disable→false | 自动(MQTT) | tc_modules.py::tc_track_enable |
| TC0682 | F0682 | 无人脸时 following | 使能跟踪但无目标 → following=false | 自动(MQTT) | tc_modules.py::tc_track_following |
| TC0683 | F0683 | actuator 恒 none | status/track.actuator == none | 自动(MQTT) | tc_modules.py::tc_track_actuator |
| TC0704 | F0704 | set_thresholds | 改 ratio 后回显新值（随后还原） | 自动(MQTT) | tc_modules.py::tc_touch_set_thresholds |
| TC0705 | F0705 | calibrate | calib.active=true（随后 cancel） | 自动(MQTT) | tc_modules.py::tc_touch_calibrate |
| TC0706 | F0706 | reset_thresholds | 命令被接受并回 thresholds | 自动(MQTT) | tc_modules.py::tc_touch_reset |
| TC0707 | F0707 | cancel_calibrate | calib.active=false | 自动(MQTT) | tc_modules.py::tc_touch_cancel_calib |
| TC0708 | F0708 | debug=true | status/touch 带 debug[] | 自动(MQTT) | tc_modules.py::tc_touch_debug |
| TC0759 | F0759 | get_keymap | 重发 status/handle/keymap | 自动(MQTT) | tc_modules.py::tc_handle_get_keymap |
| TC0761 | F0761 | set_profile | 被接受并重发 keymap（随后还原） | 自动(MQTT) | tc_modules.py::tc_handle_profile |
| TC0763 | F0763 | keymap catalog | catalog 非空 | 自动(MQTT) | tc_modules.py::tc_handle_catalog |
| TC1001 | F1001 | cmd/can/tx 门控 | allow_tx=false 时注入被拒且不崩溃 | 自动(MQTT) | tc_modules.py::tc_can_tx_gate |
| TC1002 | F1002 | cmd/can tunnel 开关 | tunnel=true→status 回显（随后关闭） | 自动(MQTT) | tc_modules.py::tc_can_tunnel |
| TC1056 | F1056 | cmd/motor/scan | 回 event/motor/scan（总线无电机也回） | 自动(MQTT) | tc_modules.py::tc_motor_scan |
| TC1252 | F1252 | dp 归一化读 | point 读值 200 且与设备一致 | 自动(HTTP) | tc_modules.py::tc_dp_read |
| TC1253 | F1253 | dp 写入校验 | 非法类型/越界被拒 | 自动(HTTP) | tc_modules.py::tc_dp_write_validate |
| TC1272 | F1272 | telemetry 默认点位 | points 含 free_heap/rssi/uptime | 自动(MQTT) | tc_modules.py::tc_telemetry_points |
| TC0321 | F0321 | ext_pins 启动提交生效 | `mode` 有效且 ∈ `supported_modes`，无 `pending_mode` 残留 | 自动(MQTT) | tc_modules.py::tc_extpin_commit |
| TC0323 | F0323 | supported_modes 白名单 | 含本固件编入的全部模式 | 自动(MQTT) | tc_modules.py::tc_extpin_supported |
| TC0324 | F0324 | apply_policy / pending 上报 | `apply_policy == restart` | 自动(MQTT) | tc_modules.py::tc_extpin_policy |
| TC0008 | F0008 | device 数据点注册齐全 | `schema.modules.device.points` ≥10 个 | 自动(MQTT) | tc_modules.py::tc_device_datapoints |
| TC0009 | F0009 | WiFi SSID/信道仅入 status | 心跳含 `wifi_ssid`(str)+`wifi_channel`(int) | 自动(MQTT) | tc_modules.py::tc_device_wifi |
| TC0010 | F0010 | 无电量来源不发伪数据 | `info/device.power.supported == false` 且无 `level_pct` | 自动(MQTT) | tc_modules.py::tc_device_power |
| TC0955 | F0955 | imu 无下行 cmd（健壮性） | 发 `cmd/imu` 后 `status/imu` 续报 | 自动(MQTT) | tc_modules.py::tc_imu_no_cmd |
| TC0958 | F0958 | imu 数据点注册 | `schema.modules.imu.points` 含 accel/gyro/pitch/roll | 自动(MQTT) | tc_modules.py::tc_imu_datapoints |
| TC1008 | F1008 | CAN 引脚上报 | `status/can.tx_gpio==38`、`rx_gpio==48` | 自动(MQTT) | tc_modules.py::tc_can_pins |
| TC0402 | F0402 | cmd/stream mode=mjpeg 门控 | HTTP 关闭时回 `http_stream_disabled` | 自动(MQTT) | tc_modules.py::tc_stream_mjpeg |

## 1.3 补充：MCP 协议级 + dp 批量端点（6 条）

| TC-ID | F-ID | 用例 | 预期 | 方式 | 脚本 |
|-------|------|------|------|------|------|
| TC1303 | F1303 | MCP initialize 握手 | 返回 protocolVersion/serverInfo（未实现则 BLOCK） | 自动(WS-MCP) | tc_wsmcp.py::tc_mcp_init |
| TC1305 | F1305 | 信封入站 `{"type":"mcp","payload":…}` | 同样得到 tools/list 结果 | 自动(WS-MCP) | tc_wsmcp.py::tc_mcp_envelope |
| TC1307 | F1307 | 超长帧（>4096B）健壮性 | 帧被拒后连接仍可用 | 自动(WS-MCP) | tc_wsmcp.py::tc_mcp_max_frame |
| TC1308 | F1308 | 响应广播到所有客户端 | A 的响应也到达 B | 自动(WS-MCP) | tc_wsmcp.py::tc_mcp_broadcast |
| TC1260a | F1260 | `POST /api/datapoints/batch` 批量读 | 200 且 data 含各点 value/ts；未知点 404 | 自动(HTTP) | tc_modules.py::tc_dp_batch_read |
| TC1260b | F1260 | `POST /api/datapoints/batch_set` 批量写 | 合法项 applied；未知点进 data.failed | 自动(HTTP) | tc_modules.py::tc_dp_batch_set |

> ✅ `batch` / `batch_set` 原为**文档有、实现无**（FINDINGS F-05）；本轮已在
> `http-server/datapoint_http.cc` **实现**并测试通过（契约见 interface/04 §3.4）。

## 1.4 补充：face/greet/handle/motor/mcp/dp 命令面（17 条，`cases/tc_modules2.py`）

| TC-ID | F-ID | 用例 | 预期 | 方式 | 脚本 |
|-------|------|------|------|------|------|
| TC0500 | F0500 | cmd/face enabled 总开关 | enabled→true；**若 internal 受限起不来则 BLOCK 附因**（F-10） | 自动(MQTT) | tc_modules2.py::tc_face_enable |
| TC0501 | F0501 | recognize_enabled | 下发后强制回帧并回显（随后还原） | 自动(MQTT) | tc_modules2.py::tc_face_recognize |
| TC0502 | F0502 | pipeline=live/identity | 回显目标 pipeline（随后还原） | 自动(MQTT) | tc_modules2.py::tc_face_pipeline |
| TC0509 | F0509 | refresh_immich | 重发 status/face/immich（未配置则 BLOCK） | 自动(MQTT) | tc_modules2.py::tc_face_refresh_immich |
| TC0526 | F0526 | MCP self.face.manage（非破坏） | 返回 result（用 refresh_immich，不动人脸库） | 自动(WS-MCP) | tc_modules2.py::tc_face_mcp_manage |
| TC1261 | F1261 | 数据点自动 MCP 工具读写回环 | MCP `set_<point>` 生效且 MQTT status 同步（单一真源）；自动工具未注册则 BLOCKED 附因（F-12） | 自动(WS-MCP+MQTT) | tc_modules2.py::tc_dp_mcp_roundtrip |
| TC1264 | F1264 | 数据点子实例 | schema 含 `module.<sub>.point`（无则 SKIP） | 自动(HTTP) | tc_modules2.py::tc_dp_sub_instances |
| TC1264b | F1264 | 子实例写入 `cmd/<module>` `<sub>_<point>` | 扁平字段翻译为 `module.<sub>.<point>` 并写入生效（status 回读目标值后还原）；无子实例 rw 点则 SKIP（当前仅 `servo.target`） | 自动(MQTT) | tc_modules2.py::tc_dp_sub_instance_write |
| TC0656 | F0656 | MCP self.face.simulate_greet | 调用成功且 `current_speaker.source=simulate`（随后 clear） | 自动(WS-MCP+MQTT) | tc_modules2.py::tc_greet_mcp_simulate |
| TC0658 | F0658 | MCP self.face.set_greet_config | `greet_gap_sec` 生效并回显（随后还原） | 自动(WS-MCP+MQTT) | tc_modules2.py::tc_greet_mcp_cfg |
| TC0659 | F0659 | cmd/face simulate_greet + clear_speaker | simulate 后 `source=simulate`；clear 后消失 | 自动(MQTT) | tc_modules2.py::tc_greet_mqtt_simulate |
| TC0661 | F0661 | cmd/face action=set_greet_config | 下发后 status/face 回显（随后还原） | 自动(MQTT) | tc_modules2.py::tc_greet_mqtt_cfg |
| TC0662 | F0662 | cmd/face 顶层 greet_gap_sec | 无 `action` 的顶层字段同样生效（随后还原） | 自动(MQTT) | tc_modules2.py::tc_greet_mqtt_top |
| TC0751 | F0751 | 输入超时清零 | 注入 `connected:true` 摇杆后停发 >1000ms → 轴归零、`connected=false` | 自动(MQTT) | tc_modules2.py::tc_handle_timeout |
| TC0760 | F0760 | 运行时轴映射 axis_bindings | `status/handle/keymap` 含 `axis_bindings` | 自动(MQTT) | tc_modules2.py::tc_handle_axis |
| TC1075 | F1075 | cmd/motor mcp_call 白名单外拒绝 | 非 motor/can 工具被拒（`mcp_result` 明确报错） | 自动(MQTT) | tc_modules2.py::tc_motor_mcp_whitelist |
| TC1262 | F1262 | telemetry 汇入多模块数据点 | `status/telemetry.points` 同时含不同模块点位 | 自动(MQTT) | tc_modules2.py::tc_telemetry_multi |
| TC1309 | F1309 | 客户端断开后服务仍可用 | 断开后新客户端 `tools/list` 正常 | 自动(WS-MCP) | tc_modules2.py::tc_mcp_lifecycle |

---

## 2. 待补充的自动化用例（覆盖缺口）

下表按域列出**尚未实现自动化**的功能点（详见 [FEATURES.md](./FEATURES.md)）。
它们并非不需要测，而是需要**串口日志断言**、**物理动作确认**、**跨仓/前端环境**或**设备当前剖面未激活**。
补测优先级：`P0`（可自动化，应尽快补齐）> `P1`（需串口/较长时序）> `P2`（物理/人工/跨仓）。
（标注 ✅ 的条目已由 §1.1 串口用例 / §1.2 命令类用例覆盖。）

| domain | 未自动化 F 覆盖 | 主要方式 | 缺口原因 / 补测计划 |
|--------|----------------|----------|---------------------|
| device | F0018, F0020, F0021, F0023–F0025 | 串口 / 内部 | retain 重发、client_id、QoS、重连需断网构造（P1）；✅F0007–F0013/F0019 已覆盖 |
| pair | F0101–F0119 | 串口 / HTTP / 人工 | 6 位码播报、45s 重播、屏显、触摸手势需人工；HTTP 配对需云端 `server_api_base`（P1/P2） |
| net | F0201–F0203, F0205, F0207, F0209–F0213 | 串口 / HTTP | 静态 IP、配网页 broker 需 AP 模式（P1）；✅F0200/F0204/F0206 已覆盖 |
| config | F0300–F0319, F0322, F0325–F0329 | HTTP / 串口 | 需 mock 配置服务器（pull/ack/注册/签名）与重启回滚构造（P1）；✅F0320/F0321/F0323/F0324 已覆盖 |
| stream | F0400, F0405–F0416, F0418–F0429 | MQTT / HTTP / RTSP | 自动停计时、拍照（需摄像头）、640×480 剖面、推流重连（P1）；✅F0401–F0404/F0417 已覆盖 |
| face | F0503–F0507, F0510–F0524, F0528–F0535 | MQTT / HTTP / WS-MCP | 人脸库 CRUD（**勿误删已入库人员**）、数字 ID 稳定性、框叠加（P0/P2）；✅F0500–F0502/F0508/F0509/F0511/F0525–F0527/F0534 已覆盖 |
| immich | F0600–F0613, F0615–F0621 | HTTP / 串口 | 需 Immich 服务器与 API Key（P2） |
| greet | F0650–F0652, F0657, F0660, F0663–F0664 | MQTT / 串口 | 主动招呼需语音链路（P2）；✅F0653–F0656/F0658/F0659/F0661/F0662 已覆盖 |
| track | — | — | ✅F0680/F0681/F0682/F0683 全部覆盖 |
| touch | F0700–F0702, F0709–F0713 | MQTT | 采样/事件/组合手势需物理按键（P2）；✅F0703–F0708/F0714 已覆盖 |
| handle | F0753–F0757, F0764–F0770 | MQTT | BT/App 应用需板载手柄（P2）；✅F0750/F0751/F0758/F0759/F0760/F0761/F0762/F0763 已覆盖 |
| led | F0800–F0803, F0805–F0809 | MQTT / WS-MCP | 需 `EXT_PIN=LED` 剖面（P1） |
| gimbal | F0850–F0859, F0861, F0862 | MQTT | 需 `EXT_PIN=PWM` 剖面；命令可空转验证（P1） |
| servo | F0900–F0903, F0905–F0907 | MQTT / WS-MCP | 需 `EXT_PIN=PWM` + `SERVO_ENABLE`（P1） |
| imu | F0950, F0951, F0954, F0957 | 串口 | 阈值/不可用态（P2）；✅F0952–F0956/F0958 已覆盖 |
| can | F1005–F1007, F1009 | MQTT / 串口 | 需 CAN 台架收到帧（P1；F1009 需 `MOTOR=0` 剖面注入帧验 `status/can/frames`）；✅F1000–F1004/F1008 已覆盖 |
| motor | F1051–F1055, F1057–F1070, F1073, F1076–F1078 | MQTT / WS-MCP | 需 CAN 电机接入（位置/速度/MIT/反馈/示教）；✅F1050/F1056/F1071/F1072/F1074/F1075 已覆盖 |
| dog | F1150–F1183 | WS-MCP / HTTP / 串口 | 需 `DOG_ENABLE=1` 剖面与四足机构（P2） |
| dp | F1250, F1251, F1254, F1255, F1265–F1267, F1269–F1271, F1273, F1274 | MQTT / HTTP / WS-MCP | MCP 归一化铁律/telemetry 节奏/子实例懒注册（P1）；✅F1252/F1253/F1256–F1264/F1268/F1272 已覆盖（F1264 子实例**写入**用例 TC1264b 已就绪，需 PWM/电机剖面才会实跑） |
| mcp | F1300, F1301, F1304, F1310, F1312–F1314, F1316 | WS-MCP | 信封/心跳/鉴权等协议项（P1）；✅F1302/F1303/F1305–F1309/F1311/F1315 已覆盖 |
| http | F1351, F1354, F1355, F1357–F1359, F1361–F1363 | HTTP | 需 `DEEP_DOG_HTTP_SERVER_ENABLE=1`（P1） |
| rtsp | F1401–F1403, F1405–F1417 | RTSP / 串口 | 需 MediaMTX 可达；S09 内存优化项（P1/P2） |

> 补测时按「域」增量往 `cases/` 加用例，并在 §1 表格追加行、更新 TEST_REPORT。
