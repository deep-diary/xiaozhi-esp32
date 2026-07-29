# I02 · 源：Bluepad32 + Xbox（板载 BLE）

| 项 | 内容 |
|----|------|
| source | `bt` |
| 手柄 | Xbox 无线控制器（优先 Series / model 1914；固件 ≥ v5.15 BLE） |
| 库 | [Bluepad32](https://github.com/ricardoquesada/bluepad32) |
| 芯片 | ESP32-S3（仅 BLE，无 Classic BR/EDR） |

## Bluepad32 角色

- **是**：手柄主机适配层（扫描、配对、解析 Xbox HID report → 统一 API）。
- **不是**：替代 ESP-IDF 蓝牙协议栈。其下仍需 **BLE Host**（优先 **NimBLE**）。
- 相对「手写 HID Host」：开发量小；Flash/RAM **不会比无协议栈更小**，通常 ≈ 栈 + 库。

## 配对（需求）

1. 手柄 Xbox 键开机 → 长按配对键约 3s。
2. 固件 `handle/cmd` `action: pair` 或开机自动扫描（实现二选一，默认：enable 后扫描）。
3. 连上后 Normalize → Hub；`handle/status` 中 `source: "bt"`。

不支持：索尼 DualShock 4 / DualSense **无线直连本板**（Classic only）。

## Flash / RAM 预算

| 资源 | 现状 | 影响 |
|------|------|------|
| App 分区 `ota_*` | **已扩至各 `0x5a0000`（~5.625 MiB）**；`assets` 减至 `0x220000` | 改表后须全量重刷分区 |
| BLE（NimBLE 量级） | 约 +150～250 KiB Flash | 分区已留余量；BT 仍未接入 |
| 内部 RAM | 人脸/推流已紧 | BT 常驻再吃内部堆；需可开关与错峰 |

### 实现前置（硬性）

1. ~~扩 OTA~~：**已完成**（见 [`16m_deep_dog.csv`](../../../../../partitions/v2/16m_deep_dog.csv)）。
2. `CONFIG_BT_ENABLED` + NimBLE / Bluepad32 — **仍 planned**；`DEEP_DOG_HANDLE_BT_ENABLE` 默认 0。
3. 当前固件已落地 **wifi 源**（`handle/input`）；本文件板载 BT 为后续任务。

## 与 Wi‑Fi 共存

- deep-dog 常驻 Wi‑Fi（MQTT / 推流）。BLE + Wi‑Fi 共存会抬高内部 RAM 与调度压力。
- 建议：人脸高峰 / 高分辨率编码时允许 `disable` 或编译关掉 BT；运控联调时再开。

## 验收（本源）

- [ ] 分区扩容后固件可链接且含 Bluepad32
- [ ] Xbox BLE 配对成功，`handle/status.source=bt`，摇杆/键可见
- [ ] `disable` 后 App 不再驱动狗；status 仍可观察（策略以实现为准）
