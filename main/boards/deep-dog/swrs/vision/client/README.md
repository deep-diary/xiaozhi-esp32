# vision/client

设备作为 **流媒体客户端**（推 MediaMTX）。**排在 Server S04/S05 之后**，并复用 `face_ai`。

| ID | 文档 | 状态 |
|----|------|------|
| V-C01 | [C01-stream-server-verify.md](./C01-stream-server-verify.md) | 基建可达；完整推拉请在稳定 LAN 复验 |
| V-C02 | [C02-device-push-stream.md](./C02-device-push-stream.md) | **固件已实现**；实机长稳待勾 |

MQTT 规格已归 **[swrs/mqtt/](../../mqtt/)**（入口卡 + 模块详情页）：

| 路线图 | 文档 |
|--------|------|
| V-C03 | [modules/02-stream.md](../../mqtt/modules/02-stream.md) |
| V-C04 | [modules/09-gimbal.md](../../mqtt/modules/09-gimbal.md) |

前端从 [mqtt/README](../../mqtt/README.md) 读起。总表见 [ROADMAP](../../ROADMAP.md)。
