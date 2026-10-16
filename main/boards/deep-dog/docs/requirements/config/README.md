# config/ — 统一配置中心

| ID | 文档 | 说明 | 状态 |
|----|------|------|------|
| CFG-01 | [统一配置中心（设备端）](./CFG-01-unified-config-center.md) | 从 deep-trace 拉取配置，模板 + 设备覆盖，双模式同步 | 需求就绪 |
| CFG-02 | [自由引出脚 38/48 运行时可配置](./CFG-02-ext-pin-runtime-mode.md) | 一版固件通吃 can/pwm/led/uart/io…；重启生效；体积实测 +22KB | P1 已落地 |
| CFG-03 | [编译期联调剖面开关（Profile）](./CFG-03-compile-time-profiles.md) | 一个 `DEEP_DOG_PROFILE` 宏选剖面（CAN 通讯 / 单电机 / 四足 / 全功能）；CAN 收帧与电机栈解耦 | P1 已落地 |

## 后端对应

- deep-trace `backend/devices/config/`（Django + DRF）
- 设计文档：`deep-trace/docs/design/unified-config-center.md`

## 跨端契约

- HTTP API：`docs/interface/03-http-api-contract.md`（B. 设备 → 云端配置 API）
- MQTT 通知：`mqtt/protocol/deep-dog-mqtt.yml`（cmd/config）
