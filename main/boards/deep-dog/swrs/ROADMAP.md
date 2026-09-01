# deep-dog 可追溯路线图

> 权威交付顺序。需求正文在 `dog/`、`vision/`、`mqtt/`、`input/`、`motor/`；本文件只维护**序号、依赖、状态、验收指针**。  
> 板型定位：**可裁剪全功能模块板**（狗控可选，见 [mqtt/M01](./mqtt/M01-board-mqtt-protocol.md)）。

## 交付顺序（拍板）

1. **设备 = HTTP 服务器**：遥控 → MJPEG → 人脸框 → **本地数字 ID** → Immich 真名  
2. **设备 = 流媒体客户端**：MediaMTX 推流，**复用**同一套 `face_ai`  
3. **板级 MQTT 契约（M01）** → 模块文档落地（stream=V-C03 等）  
4. 产品化：Kiosk / 对话个性化  

不并行把 Immich 与推流和本地 ID 绑死；视觉轨当前切片为 **V-C02**（设备推 MediaMTX，人脸永驻）。

## 总表

| 序号 | ID | 文档 | 依赖 | 状态 |
|------|-----|------|------|------|
| D1～D5 | dog 历史 | [dog/DEVELOPMENT_PLAN.md](./dog/DEVELOPMENT_PLAN.md) | — | ✅ 主体完成 |
| D6 | 力矩保护 | 同上 §阶段六 | D1～D5 | 待办 |
| D7 | 大步插值/运控 | 同上 §阶段七 | D3 | 部分进行中 |
| D8 | 晃身/横移 | 同上 §阶段八 | D5 | ✅ |
| D9 | IMU（BMI270） | 同上 §阶段九；MQTT `imu/status` | — | 待办（型号已拍板） |
| D11～D13 | RK3588 / ROS | 同上 | — | 远期 |
| **V-S01** | HTTP 狗控 | [vision/server/S01](./vision/server/S01-http-dog-motion.md) | DogControl | 主体 ✅ |
| **V-S02** | HTTP MJPEG | [vision/server/S02](./vision/server/S02-http-mjpeg.md) | S01 | ✅ |
| **V-S03** | 人脸框叠加 | [vision/server/S03](./vision/server/S03-http-face-overlay.md) | S02 | 一期 ✅ |
| **V-S04** | 本地数字 ID | [vision/server/S04](./vision/server/S04-local-face-numeric-id.md) | S03 | ✅ |
| **V-S05** | Immich 真名 | [vision/server/S05](./vision/server/S05-immich-real-name.md) | S04 | ✅ |
| **V-S06** | 预览/检测 VGA（640×480） | [vision/server/S06](./vision/server/S06-higher-resolution.md) | S02/S05 | §9 代码保留；**联调默认 240²**（8MB PSRAM 并发不足） |
| **V-S07** | 人脸 MCP + registry | [vision/server/S07](./vision/server/S07-face-control-mcp.md) | S05 · M01 | 本轮 |
| **V-S08** | RTSP+人脸 WDT 缓解 | [vision/server/S08](./vision/server/S08-rtsp-face-wdt-mitigation.md) | C02 · S04 | 已落地间隔下限 |
| **V-S09** | Internal SRAM 优化 | [vision/server/S09](./vision/server/S09-internal-sram-optimization.md) | S04/S05 · S08 · M01 device/status | **评估 ✅**（MEM-01～11 待 Agent 逐项落地） |
| V-C01 | MediaMTX 验收 | [vision/client/C01](./vision/client/C01-stream-server-verify.md) | [infra](./vision/infra.md) | 基建可达；完整推拉待稳定 LAN 复验 |
| V-C02 | 设备推流 | [vision/client/C02](./vision/client/C02-device-push-stream.md) | S04/S05、C01 | **固件已实现**（人脸永驻 + MJPEG/RTSP 互斥）；实机长稳待勾 |
| **M01** | 板级 MQTT 总览 | [mqtt/M01](./mqtt/M01-board-mqtt-protocol.md) / [YAML](./mqtt/protocol/deep-dog-mqtt.yml) / [modules](./mqtt/modules/) | infra | **文档 ✅**（入口卡+详情页规格） |
| **N01** | SNTP 时钟 | [net/N01](./net/N01-sntp-clock-sync.md) | WiFi | 本轮 |
| **N02** | STA DHCP 寻址 | [net/N02](./net/N02-sta-address.md) | WiFi | **本轮**（默认 DHCP；静态 IP 可选） |
| **N03** | 配网页 MQTT Broker | [net/N03](./net/N03-wifi-portal-mqtt-broker.md) | WiFi AP · M01 | **本轮** |
| V-C03 | MQTT 推流开关 | [mqtt/modules/02-stream](./mqtt/modules/02-stream.md) | C02、**M01** | **固件已实现**（device+stream）；实机 MQTTX 待勾 |
| V-C04 | MQTT 云台 | [mqtt/modules/09-gimbal](./mqtt/modules/09-gimbal.md) | stream 客户端、**M01** | **本轮联调**（固件已有；默认剖面 PWM + GIMBAL；实机 MQTTX 待勾） |
| **I-HANDLE** | 手柄双源输入 | [input/](./input/) · [11-handle](./mqtt/modules/11-handle.md) · [`handle/`](../handle/) | **M01**；BT 需 Bluepad32+BTstack | **wifi ✅**；BT 适配已落地（默认关）；分区已扩 |
| **I08b** | 通用轴映射 | [input/I08b](./input/I08b-axis-mapping.md) | I08a | 本轮落地 |
| **MOT-01** | CAN 硬件剖面 | [motor/01](./motor/01-can-hw-profile.md) | — | 本轮落地 |
| **MOT-02** | CAN MQTT 透传 | [motor/02](./motor/02-can-mqtt.md) · [12-can](./mqtt/modules/12-can.md) | MOT-01、M01 | 本轮落地 |
| **MOT-03** | 电机 MQTT | [motor/03](./motor/03-motor-mqtt.md) · [14-motor](./mqtt/modules/14-motor.md) | MOT-01、M01 | 本轮落地 |
| **MOT-04/05** | motor catalog + 轴样例 | [motor/04](./motor/04-handle-motor-catalog.md) · [05](./motor/05-analog-axis-sample.md) | I08b、MOT-03 | 本轮落地 |
| **MOT-14** | 电机 MCP 工具收敛 + 粘性默认电机 | [motor/14](./motor/14-motor-mcp-tools.md) | MOT-03、MOT-10 | 本轮落地 |
| **MOT-15** | 设置电机 CAN ID | [motor/15](./motor/15-set-can-id.md) | MOT-01、MOT-06、MOT-14 | 本轮落地 |
| V-P01 | Kiosk / 对话 | [vision/product/P01](./vision/product/P01-kiosk-personalization.md) | S05 | 更后 |
| **CE01** | micro-ROS 链路冒烟 | [cloud_edge/CE01](./cloud_edge/CE01-microros-link-smoke.md) · [`microros/`](../microros/) | WiFi STA；a3 E1/E9/E4 | **本轮**（阶段 A；默认关） |

## 依赖关系

```text
S01 → S02 → S03 → S04 → S05 → S06 → C02 → stream(V-C03) → gimbal(V-C04)
                    │                      ↑
                    └── face_ai ───────────┘
C01 ─────────────────────────────────────→ C02
M01 + modules/ ──────────────────────────→ 各模块 MQTT（含 V-C03/C04）
M01 + input/ ────────────────────────────→ I-HANDLE（bt / wifi 双源；固件 planned）
S05 ─────────────────────────────────────→ P01
D9 (BMI270) ─────────────────────────────→ imu/status (modules/03-imu)
WiFi STA ────────────────────────────────→ CE01 (micro-ROS Client；阶段 A)
```

## 视觉轨验收速查

| ID | 一句话验收 |
|----|------------|
| S01 | 网页可 init/前进等，不堵 HTTP |
| S02 | 可开关 MJPEG，拉流时仍可遥控 |
| S03 | Streaming + 人脸开 → Canvas 有框 |
| S04 | **不同人显示不同数字 ID**；同人稳定同一 ID；不调 Immich |
| S05 | 本地 ID 可绑定 Immich 真名；失败仍显示数字 ID |
| S06 | 采集 **640×480**（face/Immich）；RTSP **320×240** 推流降级（§9）；Immich crop 短边 ≥320；见 [S06 §9](./vision/server/S06-higher-resolution.md) |
| C02 | 设备推到 MediaMTX，外网/内网可拉；人脸逻辑复用 |

## 基础设施与 MQTT

- 地址：[vision/infra.md](./vision/infra.md)（MediaMTX / EMQX / Immich，无明文密钥）。
- 整板 Topic / 字段：[mqtt/](./mqtt/)（[M01](./mqtt/M01-board-mqtt-protocol.md) + [modules](./mqtt/modules/) 前端详情页规格 + YAML）。
