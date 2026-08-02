# I02 · 源：Bluepad32 + Xbox（板载 BLE）

| 项 | 内容 |
|----|------|
| source | `bt` |
| 手柄 | Xbox 无线控制器（优先 Series / model 1914；固件 ≥ v5.15 BLE） |
| 库 | [Bluepad32](https://github.com/ricardoquesada/bluepad32) |
| BLE Host | **[BTstack](https://github.com/bluekitchen/btstack)**（Bluepad32 固定依赖；**不是** ESP-IDF NimBLE） |
| 芯片 | ESP32-S3（仅 BLE，无 Classic BR/EDR） |
| 状态 | 固件已落地并实机验过；**日常剖面默认关**（堆预算给 MQTT/人脸/唤醒）；瘦剖面再 `-DDEEP_DOG_HANDLE_BT=ON` |

## 选型

| 方案 | 结论 | 适用 |
|------|------|------|
| **Bluepad32 + BTstack** | **首选**。ESP-IDF 原生、Xbox Series BLE 已验证；统一 `uni_gamepad_t` + `play_dual_rumble` | 板载 `source=bt` |
| 手写 NimBLE HID Host | 扫描/配对/Xbox HID 解析/重连工作量大，与 Hub 契约重复造轮 | 不推荐 |
| ESP-IDF Bluedroid/NimBLE 裸 HID | 无现成 Xbox BLE 主机适配层 | 不推荐 |
| PC MQTT 桥（已有） | [`scripts/deep_dog/deep_dog_handle_bridge.py`](../../../../../scripts/deep_dog/deep_dog_handle_bridge.py) → `handle/input` | `source=wifi` |

### Bluepad32 角色

- **是**：手柄主机适配层（扫描、配对、解析 Xbox HID report → 统一 API）。
- **不是**：ESP-IDF NimBLE/Bluedroid 的替代品；其下固定跑 **BTstack**。
- 相对「手写 HID Host」：开发量小；Flash/RAM ≈ BTstack + 库，不会比无协议栈更小。
- **授权**：Bluepad32 Apache-2.0；BTstack 对开源免费，**闭源商用需单独授权**。

### 与 BluFi / NimBLE 互斥

小智公共配网 BluFi 使用 ESP-IDF NimBLE（或 Bluedroid）。**同一进程不能同时跑两套 BLE Host**。

deep-dog 约定：

1. 打开板载手柄 BT（`DEEP_DOG_HANDLE_BT_ENABLE=1`）时，BT 仅作 **手柄主机**（BTstack）。
2. 配网 BluFi 与手柄 BT **时序互斥**：配网阶段不启 Bluepad32；手柄联调阶段不启 BluFi（或编译关掉配网 BT）。
3. 不得把文档里的「NimBLE」当作 Bluepad32 的底层栈。

## 配对（需求）

1. 手柄 Xbox 键开机 → 长按配对键约 3s。
2. 固件 `handle/cmd` `action: pair` → 启动扫描/自动连接；或 `enable` 后自动扫描（默认：enable 后扫描）。
3. 连上后 Normalize → Hub；`handle/status` 中 `source: "bt"`。

不支持：索尼 DualShock 4 / DualSense **无线直连本板**（需 Classic BR/EDR；ESP32-S3 无）。

## 输入矩阵（`uni_gamepad_t` → `HandleSnapshot`）

Xbox BLE 经 Bluepad32 归一化后，与 [`handle_types.h`](../../handle/handle_types.h) / [I01](./I01-architecture.md) 对齐：

| Bluepad32 字段 | 映射 | v0.1 |
|----------------|------|------|
| `axis_x/y`、`axis_rx/ry`（约 −512…511） | `axes.lx/ly/rx/ry` ∈ [-1,1]，**右 / 下为正** | **必做** |
| `brake` / `throttle`（0…1023） | `buttons.l2` / `r2` ∈ [0,1] | **必做** |
| `BUTTON_A/B/X/Y` | `a/b/x/y` | **必做** |
| `BUTTON_SHOULDER_L/R` | `l1`/`r1` | **必做** |
| `BUTTON_THUMB_L/R` | `l3`/`r3` | 做 |
| misc select / start / system | `select` / `start` / `ps` | 做 |
| `dpad` 位掩码 | `dpad_up/down/left/right` | 做 |
| `gyro[]` / `accel[]` | `motion.*` | **Xbox 通常无 IMU**；`present=false`，不承诺 |
| 触控板 | — | Xbox **无** |

断连：推送 `connected=false` 并清零 axes/buttons。

## 反向控制（ESP → Xbox）

ESP 是 **HID Host**，手柄是 Peripheral。可向下发输出报告；**不能**把 ESP 当成 Xbox 主机外设去操控主机/游戏。

| 能力 | Xbox Series BLE + Bluepad32 | 落点 |
|------|------------------------------|------|
| **双马达震动** | **支持**：`play_dual_rumble(delay_ms, duration_ms, weak, strong)`（FW v5.x） | v0.1：板级 `HandleBtRumble` + `handle/cmd` `rumble` |
| 扳机独立震动 | 示例有；稳定性次于主马达 | **v0.2**，不进 v0.1 验收 |
| 玩家 LED / 灯条 | Xbox 能力弱；不依赖 | 不做 |
| 操控 Xbox 主机 | 角色不对 | **排除** |

### MQTT `handle/cmd` rumble（联调）

```json
{ "action": "rumble", "duration_ms": 250, "weak": 128, "strong": 64, "ts": 1710000000 }
```

| 字段 | 说明 |
|------|------|
| `duration_ms` | 震动时长；缺省 250；建议上限 2000 |
| `weak` / `strong` | 弱/强马达幅度 0–255；缺省 128 / 64 |
| `delay_ms` | 可选延迟启动；缺省 0 |

业务侧（如 Dog 碰撞反馈）可直接调板级 API，无需经 MQTT。

线程约束：Bluepad32 / BTstack **非多线程安全**；震动须在 BTstack 线程或经 `_safe` / `btstack_run_loop_execute_on_main_thread` 调度。

## Flash / RAM 预算

| 资源 | 现状 | 影响 |
|------|------|------|
| App 分区 `ota_*` | **已扩至各 `0x5a0000`（~5.625 MiB）**；`assets` 减至 `0x220000` | 改表后须全量重刷分区 |
| BLE（BTstack + Bluepad32） | 约 +200～350 KiB Flash 量级（实测为准） | 分区已留余量 |
| 内部 RAM | 人脸/推流已紧 | BT 常驻再吃内部堆；需可开关与错峰 |

### 实现前置

1. ~~扩 OTA~~：**已完成**（见 [`16m_deep_dog.csv`](../../../../../partitions/v2/16m_deep_dog.csv)）。
2. 板级适配已落地：[`handle/sources/handle_bt.*`](../../handle/sources/handle_bt.h)（宏默认 0 为桩）。
3. **日常**用 PC 桥（[I03](./I03-source-pc-mqtt-bridge.md)），固件保持 `DEEP_DOG_HANDLE_BT=OFF`。
4. 瘦剖面再开板载 BT：
   ```bash
   ./scripts/deep_dog/deep_dog_fetch_bluepad32.sh
   idf.py -DDEEP_DOG_HANDLE_BT=ON reconfigure build
   ```
   **`fullclean` 后须再带** `-D`；烧录前停 `monitor`。详见 [`handle/sources/README.md`](../../handle/sources/README.md)。
5. wifi 源（`handle/input`）与本源 Hub 后到覆盖；震动：板载用 `action: rumble`，桥用 `action: output`（I09）。

## 与 Wi‑Fi 共存

- deep-dog 常驻 Wi‑Fi（MQTT / 推流）。BLE + Wi‑Fi 共存会抬高内部 RAM 与调度压力。
- **启动顺序**（仅开 BT 时）：`HandleBtStart` 先于 WiFi（HCI 要大块 DMA）→ WiFi/MQTT → idle 时 AFE（INTERNAL 失败则 PSRAM 栈回退）→ Face AI 延后。会抬高 INTERNAL，板级 MQTT/OTA 可能失败——**日常默认关 BT**，手柄走 PC 桥。
- **扫描**：WiFi 正常后即扫；手柄 `ready` 后停扫；断连再扫。
- sdkconfig（开 BT 时）：`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`；`BT_CTRL_BLE_MAX_ACT=3`、关 `BT_CTRL_BLE_ADV`。HCI/BTstack 栈须 INTERNAL。
- 建议：需要人脸+稳定 MQTT 时编译关掉 BT；瘦剖面运控联调再开。

## 数据流

```text
Xbox BLE ──► Bluepad32(BTstack) ──► Normalize ──► HandleEventHub
                                                      ├─► HandleApps
                                                      └─► handle/status (source=bt)
HandleApps / handle/cmd rumble ──► play_dual_rumble ──► Xbox
```

## 验收（本源）

- [x] 分区已扩；适配代码落地（默认桩；`-DDEEP_DOG_HANDLE_BT=ON` 链 Bluepad32）
- [x] 实机：Xbox BLE 配对成功，`source=bt`，摇杆/面键/肩键可见（联调 log）
- [x] `dpad_*` / `l3`/`r3` 透传进快照（status/log 分组字段）；`ps` 可选扩展
- [ ] `disable` 后 App 不再驱动狗；status 仍可观察（联调勾选）
- [ ] `handle/cmd` `rumble` 或板级 API 触发短震冒烟（联调勾选）
- [ ] 断连后 `connected=false` 且轴/键清零（联调勾选）
