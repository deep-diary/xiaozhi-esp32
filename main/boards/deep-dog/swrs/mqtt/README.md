# deep-dog MQTT（板级协议）

deep-dog 定位为 **可裁剪全功能模块板**：MQTT 按功能域拆分 Topic，网页与固件统一认本目录契约。

| 文档 | 说明 |
|------|------|
| [M01-board-mqtt-protocol.md](./M01-board-mqtt-protocol.md) | 需求：模块表、裁剪、QoS、HTTP/驱动映射、样例 |
| [protocol/deep-dog-mqtt.yml](./protocol/deep-dog-mqtt.yml) | **字段真源**（version / modules / topics） |

Broker / 地址事实源仍见 [vision/infra.md](../vision/infra.md)。实现切片：推流 [V-C03](../vision/client/C03-mqtt-stream-control.md)、云台 [V-C04](../vision/client/C04-mqtt-gimbal.md)。

**非狗项目**：关 `capabilities.dog`（及可选对狗依赖的 `track`），保留 `stream` / `face` / `imu` / `led` / `gimbal` / `touch` / `can` 等即可。

CAN 透传：sparkbot 的 GPIO38/48（原 UART）在 deep-dog 上为 TWAI；帧打包发 `can/frames` 供网页显示（对齐 deep-trace `80-can-web-tunnel`）。
