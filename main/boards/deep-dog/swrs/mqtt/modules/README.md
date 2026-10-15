# modules · 模块详情页规格

每文件对应前端详情规格（设备页入口卡点进来）。结构统一：入口文案 → Topic → 样例 → Steps（前端）→ 固件实现。

例外：

- **pairing**（[00-pairing](./00-pairing.md)）：无 Hub 卡；网页「添加设备」与后端绑定消费。
- **track** 与 **face** 同详情页（无独立入口卡）；见 [05-track](./05-track.md)、[04-face](./04-face.md)。
- **config**（[17-config](./17-config.md)）：统一配置中心的通知通道，无入口卡；配置内容走 HTTP（CFG-01），见 [CFG-01](../../config/CFG-01-unified-config-center.md)。
- **telemetry**（[15-telemetry](./15-telemetry.md)）：无入口卡；云端 ingest 消费。

阅读顺序与入口卡顺序见 [../frontend/00-device-page.md](../frontend/00-device-page.md)、[../README.md](../README.md)。
