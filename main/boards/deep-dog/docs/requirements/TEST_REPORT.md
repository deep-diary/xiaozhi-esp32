# deep-dog 全盘功能测试报告（TEST_REPORT）

> 生成时间：2026-10-16 10:42:12
> 设备：`1051db8118fc` · IP `10.99.79.59` · broker `10.99.79.54:1883` · root `deep-trace`
> 命令：`python run.py`
> 失败根因与契约冲突分析见 [catalog/FINDINGS.md](./catalog/FINDINGS.md)，用例设计见 [catalog/TESTS.md](./catalog/TESTS.md)。

## 1. 总览

| 指标 | 值 |
|------|-----|
| 用例总数 | 118 |
| PASS | 94 |
| FAIL | 11 |
| SKIP | 3 |
| BLOCKED | 10 |
| ERROR | 0 |
| 通过率（PASS / 可执行） | 89.5% |

## 2. 分域结果

| domain | PASS | FAIL | SKIP | BLOCKED | ERROR |
|--------|------|------|------|---------|-------|
| can | 3 | 2 | 0 | 0 | 0 |
| config | 3 | 3 | 0 | 0 | 0 |
| device | 13 | 5 | 0 | 0 | 0 |
| dp | 13 | 0 | 2 | 0 | 0 |
| face | 8 | 0 | 0 | 2 | 0 |
| gimbal | 0 | 0 | 0 | 1 | 0 |
| greet | 7 | 0 | 1 | 0 | 0 |
| handle | 8 | 0 | 0 | 0 | 0 |
| http | 1 | 0 | 0 | 4 | 0 |
| immich | 2 | 0 | 0 | 0 | 0 |
| imu | 4 | 0 | 0 | 0 | 0 |
| led | 0 | 0 | 0 | 1 | 0 |
| mcp | 9 | 0 | 0 | 0 | 0 |
| motor | 6 | 0 | 0 | 0 | 0 |
| net | 1 | 0 | 0 | 0 | 0 |
| pair | 1 | 0 | 0 | 0 | 0 |
| rtsp | 2 | 0 | 0 | 0 | 0 |
| servo | 0 | 0 | 0 | 1 | 0 |
| stream | 2 | 1 | 0 | 1 | 0 |
| touch | 7 | 0 | 0 | 0 | 0 |
| track | 4 | 0 | 0 | 0 | 0 |

## 3. 未通过明细（FAIL / ERROR）

| TC-ID | 功能点 | 用例 | 现象 |
|-------|--------|------|------|
| TC0004-device-health-warn | F0004 | health.ok / warn[]（心跳载荷） | 16s 内未收到含 health 的 status/device 心跳 |
| TC0005-device-clock-synced | F0005 | clock_synced 字段（心跳载荷） | 16s 内未收到含 clock_synced 的心跳 |
| TC0006-device-task-stacks | F0006 | tasks[] / task_count（心跳载荷） | 16s 内未收到含 tasks 的心跳 |
| TC0009-device-wifi-status | F0009 | WiFi SSID/信道仅入 status | 16s 内未收到含 wifi_ssid/wifi_channel 的心跳 |
| TC0011b-device-capability-wsmcp | F0011 | capabilities.ws_mcp 与实际 WS-MCP 服务一致 | WS-MCP 端口 8080 在监听，但 capabilities.ws_mcp=False |
| TC0306-config-mqtt-notify-pull | F0306 | cmd/config refresh 触发拉取 | 未收到 cmd/config/ack |
| TC0307-config-mqtt-ack | F0307 | cmd/config/ack 结果为 accepted | 未收到 ack |
| TC0308-config-unknown-action | F0307 | 未知 action → unsupported | 未收到 ack |
| TC0403-stream-cmd-errors | F0403 | 非法 cmd/stream 回错误且不崩溃 | 非法 action 未回 error |
| TC1001-can-tx-rejected | F1001 | cmd/can/tx 在 allow_tx=false 时被拒 | allow_tx 应仍为 false（注入应被拒） |
| TC1004-can-allow-tx-gate | F1004 | allow_tx 默认关闭 | allow_tx 默认应为 false |

## 4. 阻塞（BLOCKED）

| TC-ID | 功能点 | 用例 | 阻塞项 |
|-------|--------|------|--------|
| TC0402-stream-mode-mjpeg | F0402 | cmd/stream mode=mjpeg 门控 | 未见 error（可能内置 HTTP 已启用，mjpeg 被接受） |
| TC0500-face-enable | F0500 | cmd/face enabled 总开关 | face 运行时未能启动（face_boot 需连续 internal 栈，见 FINDINGS F-01/F-10）；status/face enabled=False w=0 h=0 |
| TC0509-face-refresh-immich | F0509 | cmd/face refresh_immich | refresh_immich 后未见 status/face/immich 重发（可能未配置 Immich） |
| TC0804-led-status | F0804 | status/led retain | 当前剖面未激活 led（需 EXT_PIN=led 剖面） |
| TC0860-gimbal-status | F0860 | status/gimbal retain | 当前剖面未激活 gimbal（需 EXT_PIN=pwm 剖面） |
| TC0904-servo-status | F0904 | status/servo retain | 当前剖面未激活 servo（需 EXT_PIN=pwm + SERVO_ENABLE） |
| TC1350-http-server-enable | F1350 | 内置 HTTP server（默认关） | 内置 HTTP server 未启用（DEEP_DOG_HTTP_SERVER_ENABLE=0） |
| TC1353-http-root-page | F1353 | 内置遥控页 GET / | 内置 HTTP server 未启用 |
| TC1356-http-api-dog-status | F1356 | GET /api/dog_status | 内置 HTTP server 未启用 |
| TC1360-http-api-immich-status | F1360 | GET /api/immich_status | 内置 HTTP server 未启用 |

## 5. 跳过（SKIP）

| TC-ID | 功能点 | 用例 | 原因 |
|-------|--------|------|------|
| TC0653-greet-current-speaker | F0653 | device/status.current_speaker | 当前无活跃说话人（字段按需下发） |
| TC1264-dp-sub-instances | F1264 | 数据点子实例（module.<sub>.point） | 当前无子实例点位（未接电机/舵机，懒注册未触发） |
| TC1264b-dp-sub-instance-write | F1264 | 子实例写入 cmd/<module> <sub>_<point> | 当前无子实例 rw 点（servo 需 ext_pins.mode=pwm，motor 无 rw 点；懒注册未触发） |

## 6. 全量明细

| TC-ID | 功能点 | domain | 用例 | 通道 | 预期 | 状态 | 耗时(s) | 备注 |
|-------|--------|--------|------|------|------|------|---------|------|
| TC0001-device-info-publish | F0001 | device | 上线发布 info/device retain | MQTT | info/device retain 含 firmware/board/ip/capabilities/ext_pins | PASS | 0.10 |  |
| TC0002-device-heartbeat | F0002 | device | status/device 心跳（≤10s） | MQTT | status/device 含 online/uptime_s/free_heap | PASS | 0.05 |  |
| TC0003-device-mem-report | F0003 | device | 内存分项上报（status/system） | MQTT | status/system 含 internal_free/psram_free 等 | PASS | 0.05 |  |
| TC0004-device-health-warn | F0004 | device | health.ok / warn[]（心跳载荷） | MQTT | status/device 心跳含 health.ok 布尔 | FAIL | 16.03 | 16s 内未收到含 health 的 status/device 心跳 |
| TC0005-device-clock-synced | F0005 | device | clock_synced 字段（心跳载荷） | MQTT | status/device 心跳含 clock_synced 布尔 | FAIL | 16.04 | 16s 内未收到含 clock_synced 的心跳 |
| TC0006-device-task-stacks | F0006 | device | tasks[] / task_count（心跳载荷） | MQTT | status/device 心跳含 tasks 数组与 task_count | FAIL | 16.04 | 16s 内未收到含 tasks 的心跳 |
| TC0008-device-datapoints | F0008 | device | device 数据点注册齐全 | MQTT | info/schema.modules.device.points 含 10+ 个数据点 | PASS | 0.05 |  |
| TC0009-device-wifi-status | F0009 | device | WiFi SSID/信道仅入 status | MQTT | status/device 心跳含 wifi_ssid(str) 与 wifi_channel(int) | FAIL | 16.00 | 16s 内未收到含 wifi_ssid/wifi_channel 的心跳 |
| TC0010-device-power-unsupported | F0010 | device | 无电量来源时不发伪数据 | MQTT | info/device.power.supported == false | PASS | 0.00 |  |
| TC0011-device-capabilities | F0011 | device | capabilities 能力位 | MQTT | info/device.capabilities 含 mqtt 相关键 | PASS | 0.00 |  |
| TC0011b-device-capability-wsmcp | F0011 | device | capabilities.ws_mcp 与实际 WS-MCP 服务一致 | MQTT | 端口 8080 可连时 capabilities.ws_mcp 应为 true | FAIL | 0.11 | WS-MCP 端口 8080 在监听，但 capabilities.ws_mcp=False |
| TC0012-device-system-datapoints | F0012 | device | system 数据点注册 | MQTT | info/schema 含 system.* 数据点 | PASS | 0.00 |  |
| TC0014-device-mcp-get-ip | F0014 | device | MCP self.board.get_ip | WS-MCP | 返回 ip/connected | PASS | 0.13 |  |
| TC0015-device-mcp-diagnostics | F0015 | device | MCP self.board.diagnostics | WS-MCP | 返回内存 + 任务栈快照 | PASS | 0.04 |  |
| TC0016-mqtt-topic-skeleton | F0016 | device | topic 骨架 {root}/{id}/info/device | MQTT | 订阅前缀与骨架一致且收到消息 | PASS | 0.00 |  |
| TC0017-mqtt-device-id-mac | F0017 | device | device_id 恒为 MAC 紧凑小写 | MQTT | device_id 匹配 ^[0-9a-f]{12}$ | PASS | 0.00 |  |
| TC0022-mqtt-info-schema | F0022 | device | info/schema retain 发布 | MQTT | info/schema retain 非空 | PASS | 0.00 |  |
| TC0065-device-no-cmd | F0065 | device | device 模块无下行 cmd（健壮性） | MQTT | 向 cmd/device 发消息不导致设备异常（后续心跳仍在） | PASS | 0.00 |  |
| TC0100-pair-status-retain | F0100 | pair | status/pairing retain | MQTT | status/pairing retain 含 device_id/bound | PASS | 0.05 |  |
| TC0208-net-device-info-ip | F0208 | net | device/info 广播 ip | MQTT | info/device.ip 为合法 IPv4 | PASS | 0.00 |  |
| TC0306-config-mqtt-notify-pull | F0306 | config | cmd/config refresh 触发拉取 | MQTT | cmd/config 收到并回 ack | FAIL | 10.03 | 未收到 cmd/config/ack |
| TC0307-config-mqtt-ack | F0307 | config | cmd/config/ack 结果为 accepted | MQTT | ack data.result == accepted | FAIL | 10.03 | 未收到 ack |
| TC0308-config-unknown-action | F0307 | config | 未知 action → unsupported | MQTT | ack.result == unsupported | FAIL | 10.03 | 未收到 ack |
| TC0321-config-extpin-commit | F0321 | config | ext_pins 启动提交生效（active） | MQTT | info/device.ext_pins.mode 有效且 ∈ supported_modes（提交成功、无 pending 残留） | PASS | 0.00 |  |
| TC0323-config-extpin-supported-modes | F0323 | config | supported_modes 白名单 | MQTT | 白名单含本固件编入的全部模式（默认全模式编入） | PASS | 0.00 |  |
| TC0324-config-extpin-apply-policy | F0324 | config | apply_policy / pending 上报 | MQTT | apply_policy == restart（重启生效语义固定） | PASS | 0.00 |  |
| TC0401-stream-cmd-stop | F0401 | stream | cmd/stream stop | MQTT | stop 后 status/stream 收敛到 mode=off / state=idle | PASS | 5.18 |  |
| TC0402-stream-mode-mjpeg | F0402 | stream | cmd/stream mode=mjpeg 门控 | MQTT | 内置 HTTP 关闭时回 http_stream_disabled（不静默失败） | BLOCKED | 11.03 | 未见 error（可能内置 HTTP 已启用，mjpeg 被接受） |
| TC0403-stream-cmd-errors | F0403 | stream | 非法 cmd/stream 回错误且不崩溃 | MQTT | status/stream.error 非空（随后被清空） | FAIL | 10.05 | 非法 action 未回 error |
| TC0404-stream-status | F0404 | stream | status/stream retain 字段 | MQTT | status/stream 含 state/mode | PASS | 0.00 |  |
| TC0500-face-enable | F0500 | face | cmd/face enabled 总开关 | MQTT | enabled=true → status/face.enabled=true；若因 internal SRAM 受限起不来则 BLOCK（附因） | BLOCKED | 30.59 | face 运行时未能启动（face_boot 需连续 internal 栈，见 FINDINGS F-01/F-10）；status/face enabled=False w=0 h=0 |
| TC0501-face-recognize-toggle | F0501 | face | cmd/face recognize_enabled | MQTT | recognize_enabled 下发后 status/face 回显（随后还原） | PASS | 0.56 |  |
| TC0502-face-pipeline | F0502 | face | cmd/face pipeline=live/identity | MQTT | pipeline 下发后 status/face 回显（随后还原） | PASS | 0.56 |  |
| TC0508-face-refresh-status | F0508 | face | cmd/face refresh_status 立即回帧 | MQTT | refresh_status 后 status/face 返回 | PASS | 0.05 |  |
| TC0509-face-refresh-immich | F0509 | face | cmd/face refresh_immich | MQTT | 命令被接受并重发 status/face/immich | BLOCKED | 11.03 | refresh_immich 后未见 status/face/immich 重发（可能未配置 Immich） |
| TC0511-face-registry-retain | F0511 | face | status/face/registry retain | MQTT | registry retain 含 count/entries | PASS | 0.05 |  |
| TC0525-face-mcp-set-mode | F0525 | face | MCP self.face.set_mode | WS-MCP | set_mode 调用成功 | PASS | 0.14 |  |
| TC0526-face-mcp-manage | F0526 | face | MCP self.face.manage（非破坏动作） | WS-MCP | manage action=refresh_immich 调用成功返回 result | PASS | 0.07 |  |
| TC0527-face-mcp-list | F0527 | face | MCP self.face.list | WS-MCP | 返回人脸库列表 | PASS | 0.07 |  |
| TC0534-face-interval-clamp | F0534 | face | detect_interval_ms 运行时夹紧 | MQTT | 越界值被夹紧到 [200,5000] | PASS | 0.06 |  |
| TC0614-immich-mqtt-status | F0614 | immich | status/face/immich retain | MQTT | retain 含 configured/key_len | PASS | 0.00 |  |
| TC0619-immich-no-key-echo | F0619 | immich | Immich key 不明文上行 | MQTT | status/face/immich 仅回 key_len | PASS | 0.00 |  |
| TC0653-greet-current-speaker | F0653 | greet | device/status.current_speaker | MQTT | 有说话人时下发 current_speaker | SKIP | 0.00 | 当前无活跃说话人（字段按需下发） |
| TC0654-greet-person-active | F0654 | greet | status/person/active retain | MQTT | 有活跃人物时 person/active retain 下发 | PASS | 0.05 |  |
| TC0655-greet-mcp-get-identity | F0655 | greet | MCP self.face.get_identity | WS-MCP | 返回当前身份 | PASS | 0.08 |  |
| TC0656-greet-mcp-simulate | F0656 | greet | MCP self.face.simulate_greet | WS-MCP | 调用返回成功，current_speaker.source=simulate（随后 clear） | PASS | 0.65 |  |
| TC0658-greet-mcp-set-config | F0658 | greet | MCP self.face.set_greet_config | WS-MCP | 设置 greet_gap_sec 生效（随后还原） | PASS | 0.75 |  |
| TC0659-greet-mqtt-simulate | F0659 | greet | face/cmd simulate_greet + clear_speaker | MQTT | simulate 后 current_speaker.source=simulate；clear 后消失 | PASS | 1.11 |  |
| TC0661-greet-mqtt-set-config | F0661 | greet | face/cmd action=set_greet_config | MQTT | greet_gap_sec 下发后 status/face 回显（随后还原） | PASS | 0.56 |  |
| TC0662-greet-mqtt-top-level | F0662 | greet | face/cmd 顶层 greet_gap_sec | MQTT | 顶层（无 action）下发 greet_gap_sec 同样生效（随后还原） | PASS | 0.61 |  |
| TC0680-track-enable-disable | F0680 | track | cmd/track enable/disable | MQTT | enable → enabled=true；disable → enabled=false | PASS | 0.26 |  |
| TC0681-track-status | F0681 | track | status/track 上报 | MQTT | status/track 含 enabled/following | PASS | 0.05 |  |
| TC0682-track-no-face-following | F0682 | track | 无人脸时 following=false | MQTT | 使能跟踪但检测关/无目标 → following=false 且给出原因 | PASS | 0.07 |  |
| TC0683-track-actuator-none | F0683 | track | actuator 恒为 none | MQTT | status/track.actuator == none（无真实 pan/tilt 执行器） | PASS | 0.10 |  |
| TC0703-touch-thresholds-report | F0703 | touch | 阈值始终上报 | MQTT | status/touch.thresholds 存在 | PASS | 0.05 |  |
| TC0704-touch-set-thresholds | F0704 | touch | cmd/touch set_thresholds | MQTT | 改 press_abs_ratio 后 status/touch.thresholds 回显新值（随后还原） | PASS | 0.10 |  |
| TC0705-touch-calibrate | F0705 | touch | cmd/touch calibrate | MQTT | calibrate 后 status/touch.calib.active=true（随后 cancel） | PASS | 0.15 |  |
| TC0706-touch-reset-thresholds | F0706 | touch | cmd/touch reset_thresholds | MQTT | reset 后仍能收到 status/touch.thresholds（命令被接受） | PASS | 0.00 |  |
| TC0707-touch-cancel-calibrate | F0707 | touch | cmd/touch cancel_calibrate | MQTT | cancel 后 calib.active=false | PASS | 2.06 |  |
| TC0708-touch-debug-flag | F0708 | touch | cmd/touch debug=true 带原始值 | MQTT | debug=true 后 status/touch 带 debug[] 明细 | PASS | 0.05 |  |
| TC0714-touch-status | F0714 | touch | status/touch retain | MQTT | retain 含 pressed_mask/buttons | PASS | 0.00 |  |
| TC0750-handle-input-wifi | F0750 | handle | cmd/handle/input 注入 → status/handle | MQTT | wifi 源快照被解析并在 status/handle 回显 | PASS | 0.05 |  |
| TC0751-handle-input-timeout | F0751 | handle | 输入超时清零 | MQTT | 注入摇杆后 >1s 无输入 → 轴自动归零 | PASS | 1.06 |  |
| TC0758-handle-enable-disable | F0758 | handle | cmd/handle enable/disable 门控 | MQTT | disable/enable 不报错且状态可续 | PASS | 0.56 |  |
| TC0759-handle-get-keymap | F0759 | handle | cmd/handle get_keymap | MQTT | get_keymap 后重新发布 status/handle/keymap | PASS | 1.10 |  |
| TC0760-handle-axis-bindings | F0760 | handle | 运行时轴映射 axis_bindings | MQTT | status/handle/keymap 含 axis_bindings（并按 profile 生效） | PASS | 0.00 |  |
| TC0761-handle-set-profile | F0761 | handle | cmd/handle set_profile | MQTT | set_profile 被接受并重发 keymap（随后还原原 profile） | PASS | 1.05 |  |
| TC0762-handle-keymap-retain | F0762 | handle | status/handle/keymap retain | MQTT | retain 含 schema_ver/bindings | PASS | 0.00 |  |
| TC0763-handle-keymap-catalog | F0763 | handle | keymap catalog 按 profile 过滤 | MQTT | status/handle/keymap.catalog 非空 | PASS | 0.00 |  |
| TC0804-led-status | F0804 | led | status/led retain | MQTT | LED 模式生效时 retain 含 mode/led_count | BLOCKED | 0.00 | 当前剖面未激活 led（需 EXT_PIN=led 剖面） |
| TC0860-gimbal-status | F0860 | gimbal | status/gimbal retain | MQTT | PWM 模式生效时 retain 含 pan/tilt | BLOCKED | 0.00 | 当前剖面未激活 gimbal（需 EXT_PIN=pwm 剖面） |
| TC0904-servo-status | F0904 | servo | status/servo retain | MQTT | PWM 模式生效时 retain 含 servos[] | BLOCKED | 0.00 | 当前剖面未激活 servo（需 EXT_PIN=pwm + SERVO_ENABLE） |
| TC0953-imu-switch-counts | F0953 | imu | 12 路开关计数上报（心跳载荷） | MQTT | status/imu 心跳含 switches（rot/trans） | PASS | 0.05 |  |
| TC0955-imu-no-cmd | F0955 | imu | imu 无下行 cmd（健壮性） | MQTT | 向 cmd/imu 发消息/非 JSON 不导致异常，status/imu 续报 | PASS | 1.05 |  |
| TC0956-imu-status | F0956 | imu | status/imu 上报（~10Hz） | MQTT | status/imu 含 accel/gyro 或 ok | PASS | 0.00 |  |
| TC0958-imu-datapoints | F0958 | imu | imu 数据点注册 | MQTT | info/schema.modules.imu.points 含 accel/gyro/pitch/roll | PASS | 0.00 |  |
| TC1001-can-tx-rejected | F1001 | can | cmd/can/tx 在 allow_tx=false 时被拒 | MQTT | 注入帧被拒绝且不影响总线状态（不崩溃、allow_tx 仍为 false） | FAIL | 1.50 | allow_tx 应仍为 false（注入应被拒） |
| TC1002-can-tunnel-toggle | F1002 | can | cmd/can tunnel 开关 | MQTT | tunnel=true → status/can.tunnel=true；再关（还原） | PASS | 0.11 |  |
| TC1003-can-status | F1003 | can | status/can retain | MQTT | retain 含 ok/tunnel/allow_tx | PASS | 0.00 |  |
| TC1004-can-allow-tx-gate | F1004 | can | allow_tx 默认关闭 | MQTT | status/can.allow_tx == false | FAIL | 0.00 | allow_tx 默认应为 false |
| TC1008-can-pins | F1008 | can | CAN 引脚上报（38/48） | MQTT | status/can.tx_gpio==38 且 rx_gpio==48 | PASS | 0.00 |  |
| TC1050-motor-enable | F1050 | motor | cmd/motor 使能/失能 | MQTT | enable/reset 后 status/motor 更新 | PASS | 0.05 |  |
| TC1056-motor-scan-event | F1056 | motor | cmd/motor/scan → event/motor/scan | MQTT | 扫描命令触发扫描事件（总线上无电机也应回事件） | PASS | 0.06 |  |
| TC1071-motor-status | F1071 | motor | status/motor retain | MQTT | retain 含 motors[] | PASS | 0.00 |  |
| TC1072-motor-tools | F1072 | motor | status/motor/tools retain | MQTT | 工具目录 retain 非空 | PASS | 0.05 |  |
| TC1074-motor-mcp-call | F1074 | motor | cmd/motor mcp_call 白名单透传 | MQTT | 白名单内工具返回 mcp_result；白名单外拒绝 | PASS | 0.15 |  |
| TC1075-motor-mcp-call-whitelist | F1075 | motor | cmd/motor mcp_call 白名单外拒绝 | MQTT | 非 motor/can 工具被拒（mcp_result 明确报错） | PASS | 0.10 |  |
| TC1252-dp-http-read | F1252 | dp | GET /api/datapoints/point/<id> 读值 | HTTP | device.online 返回 200 且值与设备一致 | PASS | 0.09 |  |
| TC1253-dp-http-write-validate | F1253 | dp | 归一化写校验（类型/范围） | HTTP | 非法类型/越界写入被拒（返回错误码而非 200 成功） | PASS | 0.08 |  |
| TC1256-dp-http-schema | F1256 | dp | GET /api/datapoints/schema | HTTP | 200 且返回 schema JSON | PASS | 0.13 |  |
| TC1257-dp-http-module | F1257 | dp | GET /api/datapoints/module/device | HTTP | 200 且返回 device 模块数据点 | PASS | 0.11 |  |
| TC1258-dp-http-point | F1258 | dp | GET /api/datapoints/point/device.device_id | HTTP | 200 且值等于设备 device_id | PASS | 0.09 |  |
| TC1259-dp-http-set | F1259 | dp | POST /api/datapoints/set 只读点拒绝 | HTTP | 对只读点写入返回错误码而非 200 成功 | PASS | 0.10 |  |
| TC1260a-dp-http-batch | F1260 | dp | POST /api/datapoints/batch 批量读 | HTTP | 200 且 data 同时含所求点位的 value/ts | PASS | 0.15 |  |
| TC1260b-dp-http-batch-set | F1260 | dp | POST /api/datapoints/batch_set 批量写 | HTTP | 合法项写入成功；未知点被拒并在 data.failed 列出 | PASS | 0.27 |  |
| TC1261-dp-mcp-roundtrip | F1261 | dp | 数据点自动 MCP 工具读写回环 | WS-MCP | MCP set_<point> 生效，且 MQTT status 同步反映（同一数据点单一真源） | PASS | 0.96 |  |
| TC1262-dp-telemetry-multi-module | F1262 | dp | telemetry 自动汇入多模块数据点 | MQTT | status/telemetry.points 同时含来自不同模块的点位 | PASS | 14.13 |  |
| TC1263-dp-info-schema | F1263 | dp | 数据点 schema 自动发现 | MQTT | info/schema 含 modules 段 | PASS | 0.00 |  |
| TC1264-dp-sub-instances | F1264 | dp | 数据点子实例（module.<sub>.point） | HTTP | schema 中存在含子实例的点位（如 motor.<id>.x） | SKIP | 0.12 | 当前无子实例点位（未接电机/舵机，懒注册未触发） |
| TC1264b-dp-sub-instance-write | F1264 | dp | 子实例写入 cmd/<module> <sub>_<point> | MQTT | 扁平字段 <sub>_<point> 命中 module.<sub>.<point> 并写入生效（status/<module> 的 <sub>.<point> 变为目标值，随后还原）；无子实例 rw 点则 SKIP | SKIP | 0.00 | 当前无子实例 rw 点（servo 需 ext_pins.mode=pwm，motor 无 rw 点；懒注册未触发） |
| TC1268-telemetry-publish | F1268 | dp | status/telemetry 上报 | MQTT | 已配置遥测时 status/telemetry 含 points | PASS | 0.00 |  |
| TC1272-telemetry-default-points | F1272 | dp | telemetry 默认点位 | MQTT | status/telemetry.points 含 free_heap/rssi/uptime_s 等默认点 | PASS | 0.00 |  |
| TC1302-mcp-endpoint | F1302 | mcp | WS-MCP 端点 8080 /ws 可连 | WS-MCP | ws://{ip}:8080/ws 握手成功并可 tools/list | PASS | 2.85 |  |
| TC1303-mcp-handshake | F1303 | mcp | MCP initialize 握手 | WS-MCP | initialize 返回 protocolVersion / serverInfo | PASS | 0.30 |  |
| TC1305-mcp-envelope | F1305 | mcp | 信封入站 {"type":"mcp","payload":…} | WS-MCP | 信封形态的 tools/list 同样得到响应 | PASS | 0.06 |  |
| TC1306-mcp-methods | F1306 | mcp | 标准 MCP 方法 tools/list | WS-MCP | tools/list 返回 name/description/inputSchema | PASS | 0.79 |  |
| TC1307-mcp-max-frame | F1307 | mcp | 超长帧被丢弃且连接存活 | WS-MCP | >4096B 帧被拒后，连接仍可正常 tools/list | PASS | 0.78 |  |
| TC1308-mcp-broadcast | F1308 | mcp | 响应广播到所有已连接客户端 | WS-MCP | 客户端 A 的 tools/list 响应也出现在客户端 B | PASS | 0.10 |  |
| TC1309-mcp-client-lifecycle | F1309 | mcp | 客户端断开后服务仍可用 | WS-MCP | 客户端断开不影响后续新客户端 tools/list | PASS | 3.87 |  |
| TC1311-mcp-dp-tools | F1311 | mcp | 数据点自动 MCP 工具注册 | WS-MCP | 按 schema 推导的 self.<mod>.get_/set_<point> 应全部出现 | PASS | 0.85 |  |
| TC1315-mcp-3way-consistency | F1315 | mcp | MQTT/MCP 三端一致（face set_mode） | WS-MCP | MCP 与 MQTT 都暴露 face set_mode 能力 | PASS | 0.85 |  |
| TC1350-http-server-enable | F1350 | http | 内置 HTTP server（默认关） | HTTP | 内置 /api/status 可用（开关开启时） | BLOCKED | 0.03 | 内置 HTTP server 未启用（DEEP_DOG_HTTP_SERVER_ENABLE=0） |
| TC1352-http-server-cors | F1352 | http | 数据点 REST 带 CORS / 可跨端访问 | HTTP | 响应头含 Access-Control-Allow-Origin | PASS | 0.07 |  |
| TC1353-http-root-page | F1353 | http | 内置遥控页 GET / | HTTP | 返回 HTML 遥控页（开关开启时） | BLOCKED | 0.06 | 内置 HTTP server 未启用 |
| TC1356-http-api-dog-status | F1356 | http | GET /api/dog_status | HTTP | 返回 motors[]/has_fault（内置 server + dog 激活时） | BLOCKED | 0.04 | 内置 HTTP server 未启用 |
| TC1360-http-api-immich-status | F1360 | http | GET /api/immich_status | HTTP | 返回 configured（内置 server 开启时） | BLOCKED | 0.02 | 内置 HTTP server 未启用 |
| TC1400-rtsp-infra-reachability | F1400 | rtsp | MediaMTX 主机可达 | HTTP | MediaMTX 控制端口返回 Server: mediamtx | PASS | 0.00 |  |
| TC1404-rtsp-h264-push | F1404 | rtsp | 设备 RTSP 推流（H.264） | MQTT | cmd/stream start 后进入 streaming（有 RTP），否则给出可读 error/阻塞原因 | PASS | 2.17 |  |
