# CFG-02 · 自由引出脚（GPIO38/48）运行时可配置

| 项 | 内容 |
|----|------|
| ID | CFG-02 |
| 领域 | config（配置中心）· 板级硬件剖面 |
| 依赖 | [CFG-01 统一配置中心](./CFG-01-unified-config-center.md) · [MOT-01 CAN 硬件剖面](../motor/01-can-hw-profile.md) · [M01](../mqtt/M01-board-mqtt-protocol.md) |
| 代码 | `config_center/`（配置项）+ 板级 `config.h` / `board_features.h` / `esp_sparkbot_board.cc` |
| 后端 | deep-trace `backend/devices/config/`（配置模板新增 `io.ext_pins`） |
| 状态 | 需求就绪，待实现 |

---

## 1. 目标

让 **GPIO38 / GPIO48** 的功能（`can` / `pwm` / `led` / `uart` / `rs485` / `io` / `ad` / `none`）
**由配置中心下发，而不是重编译固件**：

- 同一版固件可服务不同功能形态（单电机 / 云台 / 灯带 / 串口 / 纯 IO …）
- 下发走 **CFG-01 配置中心**（HTTP `config/pull` 为唯一内容通道，MQTT 仅通知）
- **v1 生效方式 = 重启生效**（不做运行中热切换）
- 切换失败必须可回滚，不把设备变成"外设不可控"

## 2. 现状（为什么要改）

38/48 目前是**三处编译期硬绑定**，改功能 = 改宏 + 重编 + 重烧：

| 层 | 现状 | 位置 |
|----|------|------|
| 模式选择 | `#define DEEP_DOG_EXT_PIN_MODE` + `#if` 派生 `*_AVAILABLE` | [`config.h`](../../config.h) |
| 软件栈裁剪 | 分层钳位 + 实现 TU 整文件 `#if DEEP_DOG_*_ENABLE` 门控 | [`board_features.h`](../../board_features.h) |
| 初始化 | `#if` 逐层 `InitializeExtPinModules()`，**无 deinit / 无切换** | [`esp_sparkbot_board.cc`](../../esp_sparkbot_board.cc) |
| 上报 | `mode` 为编译期字符串常量，`capabilities` 建时定 | [`device_mqtt.cc`](../../mqtt/modules/device_mqtt.cc) |

## 3. 体积可行性（已实测）

方案「全模式编入单一固件」的**唯一硬约束是 flash**。实测（ESP-IDF 5.5，esp32s3，app 分区 0x5a0000）：

| 形态 | app bin | 余量 |
|------|---------|------|
| 当前默认（`EXT_PIN=CAN` 单电机剖面） | 5,549,120 B（`0x54ac40`） | 349,120 B（`0x553c0`，6%） |
| **全模式编入**（CAN+MOTOR+DOG+ARM+GIMBAL+LED+UART+RS485+IO+AD） | 5,571,136 B（`0x550240`） | 327,104 B（`0x4fdc0`，6%） |
| **增量** | **+22,016 B（≈21.5 KB）** | — |

**结论：体积不是障碍。** 原因：`dog/` `leg/` `trajectory/` `servo/` `gimbal/` `led/` `uart/` `io_ext/` `ad/` `rs485/` 均为薄封装，大块代码（vision / face_ai / 音频）不随 38/48 模式变化。

> 复现实验：在 `config.h` 把所有 `*_AVAILABLE` 置 1、`board_features.h` 把所有 `*_ENABLE` 置 1，`idf.py build` 读 `check_sizes` 输出；实测后须还原。
> 限制：仅测 flash；**RAM / PSRAM 不叠加**（运行时只初始化一种模式）。
> 注：`SERVO` 与 `GIMBAL` 同脚互斥，实测组合含 `GIMBAL` 不含裸舵机栈；两者共用 `Servo` 驱动，差值可忽略。

## 4. 设计

### 4.1 固件形态：从「互斥裁剪」改为「全集编入 + 运行时选择」

- 编译期：38/48 相关软件栈**全部编入**（`*_ENABLE` 固定为 1，`*_AVAILABLE` 由"编入"语义决定，不再由单一 `mode` 推导）。
- 运行期：由**生效模式**决定初始化哪一个栈，其余不初始化。
- 保留编译期裁剪能力用于**其他**剖面（如瘦剖面关 vision），但**不再用于 38/48 模式**。

### 4.2 模式解析优先级

```text
1. NVS / 配置中心下发值  io.ext_pins.mode   ← 最高
2. 编译期默认 DEEP_DOG_EXT_PIN_MODE          ← fallback（无网络/无缓存首启）
3. 兜底 none                                 ← 值非法或初始化失败
```

约束：

- 下发值必须在 `supported_modes` 白名单内，否则**拒绝并回滚**到上一生效值。
- `gpio_a` / `gpio_b` **只读**，不接受下行修改（板级硬件固定 38/48）。

### 4.3 生效方式（v1 = 重启生效）

```text
后端改配置 → MQTT cmd/config{action:refresh}
  → 设备 HTTP config/pull → 解析 io.ext_pins.mode
  → 与当前 active_mode 不同 → 写入 NVS（pending_mode）+ 上报 device/info
  → 设备重启 → 启动时按 pending_mode 初始化 → 成功则 active_mode = 该值
  → HTTP config/ack {status, applied_mode}
```

必须遵守：

- **不热切换**：不做运行中 deinit/切换（CAN 上可能挂电机，30/48 上可能有 PWM 负载）。
- `pending_mode` 与 `active_mode` 必须分别上报，前端据此显示"重启后生效"。
- 至少一次重启尝试失败 → **自动回滚**到上一个成功的 `active_mode` 并上报失败原因（防变砖）。

### 4.4 硬件互斥矩阵（单模式独占）

| 模式 | GPIO38（A） | GPIO48（B） |
|------|-------------|-------------|
| `can` | TWAI TX | TWAI RX |
| `uart` | TXD | RXD |
| `rs485` | TX | RX（占位） |
| `pwm` | 舵机 PAN / 云台 X | 舵机 TILT / 云台 Y |
| `io` | IO-A | IO-B |
| `ad` | AD-A（占位） | AD-B（占位） |
| `led` | WS2812 DIN | 空闲保留 |
| `none` | 空闲 | 空闲 |

互斥规则（须在解析层校验）：

1. 同一引脚同一时刻**只能被一个外设占用** → 模式天然互斥。
2. `io` 与 `led` **不共存**（`led` 是独立模式，不是 `io` 的用法）。
3. `pwm` 下 `gimbal` 与 `servo` 互斥（`GIMBAL_ENABLE` 优先）。
4. `pwm != mode` 时 `capabilities.gimbal/servo` 必须为 `false`。

### 4.5 capabilities 运行期化

`capabilities` 与 `ext_pins.mode` 必须**自洽**，由生效模式在启动后计算，不得再是编译期常量：

| 生效模式 | 应置 true | 应置 false |
|----------|-----------|------------|
| `can` | `can`, `motor`（若编入） | `servo`, `gimbal`, `led`, `uart` |
| `pwm` | `servo` 或 `gimbal` | `can`, `motor`, `led`, `uart` |
| `led` | `led` | `can`, `motor`, `servo`, `gimbal`, `uart` |
| `uart` | `uart` | `can`, `motor`, `servo`, `gimbal`, `led` |
| `io` / `ad` / `none` | — | 以上全部 |

切换生效后必须**重发 `device/info`（retain）**，否则前端入口卡显隐会滞后。

## 5. 契约（跨端）

### 5.1 配置项（`device.config.v1` 新增，全部可选）

```json
{
  "config": {
    "io": {
      "ext_pins": {
        "mode": "can"
      }
    }
  }
}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `io.ext_pins.mode` | string | 否 | 目标模式；白名单见 §4.4；非法值拒绝并回滚 |

- **旧固件忽略未知字段** → 不产生兼容问题（见 CFG-01 §7）。
- 设备端不合并模板，只解析最终值（CFG-01 §3.1）。
- `gpio_a` / `gpio_b` **不出现在配置项中**（只读）。

### 5.2 上行 `device/info.ext_pins`（新增字段，向后兼容）

| 字段 | 类型 | 说明 |
|------|------|------|
| `mode` | string | **当前生效**模式（语义不变，等价 `active_mode`） |
| `gpio_a` / `gpio_b` | int | 不变（38 / 48） |
| `supported_modes` | string[] | **本固件实际编入**的模式白名单，如 `["can","pwm","led","uart","rs485","io","ad","none"]`；前端据此禁用不可选项 |
| `pending_mode` | string \| null | 已下发、待重启生效；无则 `null` |
| `apply_policy` | string | 本固件生效策略，v1 固定 `"restart"` |

### 5.3 错误码

| code | 触发 | 处理 |
|------|------|------|
| `E_UNSUPPORTED_MODE` | `mode` 不在 `supported_modes` | 拒绝，回滚，`ack.status=partial` |
| `E_MODE_CONFLICT` | 模式与 `io`/`led`/`gimbal` 互斥规则冲突 | 拒绝，回滚 |
| `E_APPLY_FAILED` | 重启后目标模式初始化失败 | 回滚到上一 `active_mode` |
| `E_ROLLBACK_OK` | 已自动回滚（提示性，非错误） | 上报当前 `mode` + 原因 |
| `E_RO_PIN` | 试图修改 `gpio_a`/`gpio_b` | 忽略该字段，`ack.status=partial` |

### 5.4 通道：不新增 MQTT 业务 Topic

- 配置**内容**走 HTTP（CFG-01 §4.3），MQTT 只发 `cmd/config{action:"refresh"}`。
- 因此 CFG-02 **不需要**新增 `cmd/xxx` 下行 Topic，也不需要新模块文档。
- 生效结果通过既有 `POST /devices/config/ack` 上报（`errors[]` 携带上述错误码）。

## 6. 错误处理与回滚

| 场景 | 处理 |
|------|------|
| 无网络 / 无 NVS 缓存（首启） | 用编译期默认 `DEEP_DOG_EXT_PIN_MODE` 启动 |
| 下发值非法 | 拒绝 + 回滚，`capabilities`/`mode` 保持上一值 |
| 重启后初始化失败 | 自动回滚到上一成功 `active_mode`，重发 `device/info`，`ack.status=failed` |
| 连续回滚后仍失败 | 落到 `none`（两个脚都不占用），保证设备可连网可控 |
| 配置解析失败 | 沿用上一版本配置（CFG-01 §7） |

## 7. 兼容性

- **固件 → 后端**：`supported_modes` 为新增可选字段，旧前端忽略即可。
- **后端 → 旧固件**：`io.ext_pins` 是未知字段，旧固件按 CFG-01 §7 忽略，行为不变。
- **同版本回滚**：`mode` 语义不变（仍是生效模式），前端 `ext_pins.mode` 切页逻辑不破坏。

## 8. 验收

- [ ] 后端下发 `io.ext_pins.mode=can` → 设备上报 `pending_mode=can`，重启后 `mode=can`
- [ ] 重启后 `capabilities.can=true`、`gimbal/servo/led=false`，与 `mode` 自洽
- [ ] 下发非法值（如 `mode="spi"`）→ `ack.status=partial` + `E_UNSUPPORTED_MODE`，设备 `mode` 不变
- [ ] 下发 `gpio_a=99` → 忽略，`ack.status=partial` + `E_RO_PIN`，`gpio_a` 仍为 38
- [ ] 单版固件可依次切到 `pwm` / `led` / `uart` / `io` / `none` 并各自可用
- [ ] 目标模式初始化失败时自动回滚且设备仍可连网（`device/info` 正常）
- [ ] **回归**：切换过程不影响与 38/48 无关的功能（vision / face_ai / audio / MQTT / touch 正常）
- [ ] **回归**：`none` 模式与旧「前端壳」剖面行为一致

## 9. 非目标

- 不做运行中热切换（v1）；热切换列为 v2 评估项，前提是先实现各驱动 deinit + 业务停机。
- 不支持运行时改 `gpio_a` / `gpio_b`（板级固定 38/48）。
- 不把 38/48 之外的板级引脚纳入运行时配置。
- 不在 MQTT 上新增 38/48 专用下行 Topic（复用 CFG-01 通道）。

## 10. 落地切分

| 阶段 | 内容 | 前置 |
|------|------|------|
| **P1** | 编译期全集编入（`*_ENABLE` 固定 1，`*_AVAILABLE` 语义调整）+ 运行时模式解析 + `pending_mode`/重启生效 + `supported_modes` 上报 + capabilities 运行期化 | 本文档 |
| **P2** | 配置中心接入 `io.ext_pins` + 后端模板 + ack 错误码贯通 | P1 · CFG-01 落地 |
| **P3** | 失败自动回滚 + `none` 兜底 + 全模式回归用例 | P2 |
| v2 | 热切换（deinit + 业务停机） | P3 |
