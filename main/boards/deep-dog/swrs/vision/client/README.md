# vision/client

设备作为 **流媒体客户端**（推 MediaMTX）。**排在 Server S04/S05 之后**，并复用 `face_ai`。

| ID | 文档 | 状态 |
|----|------|------|
| V-C01 | [C01-stream-server-verify.md](./C01-stream-server-verify.md) | 基建可达；完整推拉请在稳定 LAN 复验 |
| V-C02 | [C02-device-push-stream.md](./C02-device-push-stream.md) | **固件已实现**（Hub + RTSP H.264 Push）；实机长稳待勾 |
| V-C03 | [C03-mqtt-stream-control.md](./C03-mqtt-stream-control.md) | 待办：映射 `/api/vision_publish`；完整 `stream/status` + 关联 `face/status`；协议见 [M01](../../mqtt/M01-board-mqtt-protocol.md) |
| V-C04 | [C04-mqtt-gimbal.md](./C04-mqtt-gimbal.md) | 更后：协议已细化（absolute/relative + status）；依赖 M01 |

整板 MQTT 模块（led/imu/touch/handle 等）：[../../mqtt/](../../mqtt/)。  
实现说明见 [`../../vision/README.md`](../../../vision/README.md)。总表见 [ROADMAP](../../ROADMAP.md)。
