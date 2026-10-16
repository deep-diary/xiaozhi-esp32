# CFG-03 · 编译期联调剖面开关（Profile）

| 项 | 内容 |
|----|------|
| ID | CFG-03 |
| 领域 | config（配置中心）· 板级**编译期**裁剪 |
| 依赖 | [CFG-02 引出脚运行时模式](./CFG-02-ext-pin-runtime-mode.md) · [MOT-02 CAN MQTT](../motor/02-can-mqtt.md) · [MOT-03 电机 MQTT](../motor/03-motor-mqtt.md) |
| 代码 | [`board_features.h`](../../../board_features.h)（剖面预设）· [`esp_sparkbot_board.cc`](../../../esp_sparkbot_board.cc)（CAN 收帧解耦）· [`config.h`](../../../config.h)（引脚与默认模式） |
| 文档 | [`FEATURE_FLAGS.md`](../../../FEATURE_FLAGS.md)（剖面表）· [catalog/FEATURES.md](../catalog/FEATURES.md) |
| 状态 | **P1 已落地（设备端）**：剖面开关 + CAN 收帧与电机解耦 |

---

## 1. 目标

1. **削减联调期常驻内存**：没在测电机/四足时，不要把电机栈与四足栈编进固件常驻。
2. **一处改开关**：用**一个剖面宏**代替"同时记住 `CAN/MOTOR/DOG/ARM` 四个 `*_ENABLE` + 依赖钳位"。
3. **不误伤 CAN 通讯联调**：裁掉电机栈后，CAN 收发帧与 Bus-Off 自愈**必须仍然可用**（本轮发现并修的缺陷，见 §3.2）。
4. 不破坏既有业务：引出脚**运行时**模式（CFG-02）与其余模块行为不变。

## 2. 现状与问题

### 2.1 宏太多、依赖链要背

`board_features.h` 分三层：引出脚外设 → CAN 产品链 → 与脚位无关。改一个能力要同时判断：

- 该宏默认值是多少（`#ifndef` 兜底）；
- 是否会被下层钳位（`!CAN → MOTOR=0`、`!MOTOR → DOG/ARM=0`）；
- 是否有 `*_AVAILABLE` 参与（`config.h`）。

**典型误判**：`DEEP_DOG_DOG_ENABLE` / `DEEP_DOG_ARM_ENABLE` 的**默认值本来就是 0**
（[board_features.h L47–L52](../../../board_features.h#L47-L52)），"关掉狗/机械臂"其实无可关；
真正常驻的是 **CAN + MOTOR**。这类信息只能靠读注释，缺少"改哪一行最省事"的入口。

### 2.2 谁吃内存：编译期 vs 运行期（本轮核实）

| 类别 | 模块 | 说明 |
|------|------|------|
| 常驻 RAM（开机即占） | CAN（TWAI + `can_rx` 任务 4096B 栈）、MOTOR（`DeepMotor` 对象 + motor MQTT/数据点/MCP） | 与"测不测电机"无关，只要编入且生效模式为 `can` 就建 |
| 常驻 RAM（与脚位无关） | 板级 MQTT、WS-MCP httpd（实测 ~5KB internal）、IMU、Handle、VisionHub、相机驱动、AFE | 见 [S09](../vision/server/S09-internal-sram-optimization.md) |
| **只编入、不常驻** | UART / RS485 / IO / AD / LED / SERVO+GIMBAL | 实现 TU 薄，仅当**生效模式**匹配才 init（CFG-02） |
| **默认已为 0** | DOG / LEG / TRAJECTORY / ARM | 整文件 `#if` 空 TU，不占 RAM；`DOG=0` 时 `leg/`、`trajectory/` 一并空编译 |

> 结论：联调期想省常驻内存，**唯一有意义的动作是把 CAN 产品链从 `CAN+MOTOR` 降到 `CAN`**（或整个裁掉 CAN）。

**本轮实测（ESP-IDF 5.5 / esp32s3 / 本机 2026-10-16）**：

| 剖面 | app bin | 与 `PROFILE_MOTOR` 差 |
|------|---------|----------------------|
| `PROFILE_CAN_COMM`（新默认） | 5,542,288 B（`0x549590`） | **−88,288 B（≈86 KB）** |
| `PROFILE_MOTOR` | 5,630,576 B（`0x55e670`） | — |

两者均 `idf.py build` 通过；flash 收益来自 `motor/`（协议/示教/控制/数据点）+ motor MQTT 模块整体空编译。

**运行期实测（2026-10-16，烧录后串口）**：

| 观测 | 结果 |
|------|------|
| 板级 MQTT 启动行 | `board MQTT started (connected=1) … handle=1 can=1 motor=0` → **`can=1` 且 `motor=0` 已生效** |
| internal 空闲 | `sram(internal): free=12899 min=1555`（S09 §2.1 同剖面旧基线为 8.7～11.1KB）；无 `low_internal_heap` |
| 未取证 | 串口回滚（scrollback）未截到 `ext_pins mode=can …` 与任务列表行，`can_rx` 任务存在性由代码路径 + `mode=can` 应用成功推定；**建议台架注入一帧验 `status/can/frames`** |

### 2.3 隐藏缺陷：CAN 收帧被 MOTOR 绑死

`CanRxTask` 是**唯一的真实总线读帧者**（`ESP32Can.readFrame` → `DeepDogCanNotifyFrame` → `can_frame_hub` → MQTT tunnel），
但它的**定义与创建都被 `CAN_ENABLE && MOTOR_ENABLE` 双重门控**：

- 定义：[esp_sparkbot_board.cc L336](../../../esp_sparkbot_board.cc#L336)
- 创建：[esp_sparkbot_board.cc L972](../../../esp_sparkbot_board.cc#L972)

后果：只把 `MOTOR_ENABLE` 改 0 之后——

- `cmd/can/tx` 仍能发（`can_mqtt` 直接用 `ESP32Can.writeFrame`）；
- 2s 心跳（模拟 RX）仍在（`can_mqtt` 自带，只依赖 `CAN_ENABLE`）；
- **但总线真实报文一条都收不到**，`status/can.rx_frames` 恒为 0；
- **Bus-Off 自愈守护（`recover()`/`restart()`）一并消失**，总线异常后不再自愈。

即"看起来 CAN 还在，其实只剩半条链路"——正是本轮联调最容易被误判的故障模式。

## 3. 设计

### 3.1 剖面开关（`board_features.h` 顶部，改一行）

```c
#define DEEP_DOG_PROFILE_CAN_COMM 1   // CAN 透传联调：CAN 1 / MOTOR 0 / DOG 0 / ARM 0
#define DEEP_DOG_PROFILE_MOTOR    2   // 单电机调试：CAN 1 / MOTOR 1 / DOG 0 / ARM 0
#define DEEP_DOG_PROFILE_DOG      3   // 四足运控：  CAN 1 / MOTOR 1 / DOG 1 / ARM 0
#define DEEP_DOG_PROFILE_FULL     4   // 全功能：    不改 CAN 产品链（沿用下方默认值）

#ifndef DEEP_DOG_PROFILE
#define DEEP_DOG_PROFILE DEEP_DOG_PROFILE_CAN_COMM   // ← 只改这一行
#endif
```

| 预设 | CAN | MOTOR | DOG | ARM | 隐含 `EXT_PIN_MODE` | 适用 |
|------|-----|-------|-----|-----|---------------------|------|
| `PROFILE_CAN_COMM`（**默认**） | 1 | 0 | 0 | 0 | `CAN` | CAN 透传 / 总线联调；不背电机栈 |
| `PROFILE_MOTOR` | 1 | 1 | 0 | 0 | `CAN` | 单电机调试（原默认剖面） |
| `PROFILE_DOG` | 1 | 1 | 1 | 0 | `CAN` | 四足运控（含 `leg/`、`trajectory/`） |
| `PROFILE_FULL` | 不干预 | 不干预 | 不干预 | 不干预 | **不干预**（沿用 `config.h`） | 需要手工微调时用，等价于只改下方 `*_ENABLE` |

约定：

1. 剖面块**只做"预设赋值"**，放在文件最上方；下方 `#ifndef` 默认值与依赖钳位**逻辑不动**——
   已被剖面显式定义的宏不会被 `#ifndef` 覆盖，用户仍可用 `-DDEEP_DOG_MOTOR_ENABLE=1` 单独覆盖。
2. 剖面**只管 CAN 产品链**（`CAN/MOTOR/DOG/ARM`）+ 隐含的**编译期兜底** `EXT_PIN_MODE`（§3.2）。
   引出脚其它栈（`UART/RS485/IO/AD/LED/SERVO/GIMBAL`）保持全编入，
   由 **CFG-02 运行时模式**决定是否初始化，不随剖面裁剪。
3. 需要真正裁剪 flash 时，仍可直接把某个 `*_ENABLE` 置 0（`supported_modes` 会同步收窄，见 CFG-02 §4.1）。

### 3.2 剖面同时携带「编译期兜底引出脚模式」

`DEEP_DOG_EXT_PIN_MODE`（`config.h`）只是**无 NVS 时的兜底模式**，CFG-02 后由运行时模式接管。
但它与剖面**必须自洽**：例如裁掉 CAN 产品链却仍兜底 `can`，首启 `ResolveBootTarget()` 会取到
白名单里没有的 `can` → apply 失败 → 回滚 `none`（日志 `E_APPLY_FAILED`）。

因此剖面块顺带给出该兜底值，免去"改产品链 + 忘记改默认模式"的手工联动：

```c
#if defined(DEEP_DOG_EXT_PIN_MODE) && (DEEP_DOG_PROFILE == DEEP_DOG_PROFILE_CAN_COMM ||  \
                                       DEEP_DOG_PROFILE == DEEP_DOG_PROFILE_MOTOR ||     \
                                       DEEP_DOG_PROFILE == DEEP_DOG_PROFILE_DOG)
#undef DEEP_DOG_EXT_PIN_MODE
#undef DEEP_DOG_EXT_PIN_MODE_STR
#define DEEP_DOG_EXT_PIN_MODE DEEP_DOG_EXT_PIN_CAN
#define DEEP_DOG_EXT_PIN_MODE_STR "can"
#endif
```

约束与原因：

1. **必须 `#undef` 后重定义（含 `*_MODE_STR`）**：`board_features.h` 是被 `config.h` 在 mode 默认值与
   `#if/#elif` 生成的 `DEEP_DOG_EXT_PIN_MODE_STR` **之后**才 `#include` 的（[config.h L67](../../../config.h#L67)）。
   不 `#undef` 会宏重定义告警，且 `DEEP_DOG_EXT_PIN_MODE_STR` 仍是旧值 → 启动日志 `default=` 与实际兜底模式不一致。
2. **`defined(DEEP_DOG_EXT_PIN_MODE)` 守卫**：防止有人按禁止姿势直接 include `board_features.h`
   （此时 `DEEP_DOG_EXT_PIN_CAN` 未定义）导致编译期报错。
3. **`PROFILE_FULL` 不干预**：要手工指定 mode（云台/灯带/串口剖面）时选它，`config.h` 与 `-D` 覆盖照旧生效。
4. 顺序依赖：剖面块位于 `board_features.h` 顶部，仍在 `*_ENABLE` 默认值与钳位之前，两者互不干扰。

### 3.3 CAN 收帧与电机栈解耦（本次代码变更）

| 位置 | 现状 | 改为 |
|------|------|------|
| `can_rx_task_handle_` 成员 | `#if CAN_ENABLE && MOTOR_ENABLE` | `#if CAN_ENABLE` |
| `CanRxTask` 定义 | `#if CAN_ENABLE && MOTOR_ENABLE` | `#if CAN_ENABLE`（函数内电机相关分支再套 `#if MOTOR_ENABLE`） |
| 任务创建 | 在 `#if MOTOR_ENABLE` 内 | 移到 `#if CAN_ENABLE`，任务参数 = `MOTOR_ENABLE ? deep_motor_ : nullptr` |

函数体本来就有 `if (motor)` 判空，故 `nullptr` 天然安全；`MOTOR=0` 时仅"帧 → 电机状态解算"不执行，
**收帧 / tunnel / `can_frame_hub` / Bus-Off 自愈全部保留**。

### 3.4 依赖关系（不变）

```text
PROFILE（预设）→ CAN_ENABLE → MOTOR_ENABLE → DOG_ENABLE | ARM_ENABLE
                （下方 *_ENABLE 默认值 + 钳位逻辑保持原样）
```

## 4. 影响面（须确认不破坏既有业务）

| 变更 | 影响 | 说明 |
|------|------|------|
| 默认剖面 = `PROFILE_CAN_COMM` → `MOTOR=0` | `capabilities.motor=false`；`motor/*` MQTT、数据点、MCP 工具**不注册** | 前端电机调试页应禁用（deep-trace 同步项，见 §7） |
| `MOTOR=0` | `DeepMotor` 对象与 `can_rx` 4096B 栈不再创建 | **预期收益**（省 internal） |
| `MOTOR=0` | 无电机反馈解析；`status/motor` 无数据 | 本轮不测电机，符合预期 |
| `CAN=1`（保持） | `can_mqtt` / tunnel / `status/can` / 心跳 / 自愈 | **与改动前一致** |
| `DOG/ARM` | 默认仍 0 | 无变化 |
| 引出脚运行时模式 | 不受影响 | 白名单仍含 `can`（`CAN_ENABLE=1`）；编译期兜底模式由剖面给出（§3.2），与 `config.h` 原值同为 `can`，**行为一致** |
| 其余模块（vision/face/imu/handle/MQTT…） | 不受影响 | 本轮**不裁**，另见 S09 |

> **注意**：`DEEP_DOG_EXT_PIN_MODE` 现在由剖面派生（§3.2）。若将来新增"不含 CAN"的剖面，
> 把该剖面映射到 `DEEP_DOG_EXT_PIN_NONE` 即可，**不必再手工改 `config.h`**；
> 若走 `PROFILE_FULL` + 手工 `-DDEEP_DOG_CAN_ENABLE=0`，则仍需自查兜底模式
> （[`ResolveBootTarget()`](../../../ext_pin/ext_pin_mode.cc#L153-L165) **不校验白名单**，否则首启 `E_APPLY_FAILED` → 回滚 `none`）。

## 5. 验收

- [ ] `idf.py build` 通过；启动日志匹配 `ext_pins mode=can ... can=1 motor=0 dog=0 arm=0`
- [ ] `device/info`：`capabilities.can == true`、`capabilities.motor == false`
- [ ] `ext_pins.supported_modes` 仍含 `can`
- [ ] 台架注入一帧 → `status/can/frames`（tunnel 打开时）出现该帧（**证明收帧未被 MOTOR 门控掐断**）
- [ ] 总线异常后日志仍出现 `CAN bus-off: recovery attempt …` / `CAN back to RUNNING`
- [ ] 把 `DEEP_DOG_PROFILE` 改为 `PROFILE_MOTOR` 重编 → 电机功能恢复（`capabilities.motor == true`）
- [x] **剖面派生 mode 生效**（2026-10-16 预处理验证）：对 `esp_sparkbot_board.cc` 注入
      `-DDEEP_DOG_EXT_PIN_MODE=0` 后 `-E`，日志实参为 `ModeToStr(applied), "can"`（即剖面已把兜底模式改回
      `can`、`*_MODE_STR` 同步），非 `"none"`；且默认构建无宏重定义告警、app 体积不变（`0x549590`）

## 6. 不包含

- 不做**运行时**剖面切换（编译期开关；运行时选择见 CFG-02）。
- 不裁 vision / face / imu / handle / MQTT（内存优化另见 S09）。
- 不改引脚定义、不改 MQTT 契约字段语义（仅 `capabilities.motor` 随剖面取值）。

## 7. 相关文件与跨端同步

| 类别 | 路径 |
|------|------|
| 代码 | [`board_features.h`](../../../board_features.h) · [`esp_sparkbot_board.cc`](../../../esp_sparkbot_board.cc) · [`config.h`](../../../config.h) |
| 板级文档 | [`FEATURE_FLAGS.md`](../../../FEATURE_FLAGS.md)（§引出脚外设 ENABLE / §联调剖面） |
| 注册表 | [catalog/FEATURES.md](../catalog/FEATURES.md)（`F0328` / `F1009`）· [catalog/TESTS.md](../catalog/TESTS.md) |
| 跨端（deep-trace） | `D:\working\test-platform\deep-trace-com\docs\requirements\features\iot\modules\device\190-module-device.md`（本机 Windows；`capabilities.*` 随编译剖面取值 + 样例注解）与 `...\modules\motor\235-module-motor.md`（显示条件 `mode=can && capabilities.motor===true`，**无需改动**，已天然兼容） |

> 跨端同步结论：前端 Hub 卡显隐早已按 `capabilities.<id> === true` 判定（REQ-IOT-145），
> 故 `PROFILE_CAN_COMM` 下"`mode=can` 但无电机卡"**不会**被前端误判为异常；
> 本次只需把"默认剖面已变更"写入设备模块需求，避免联调/验收当作 Bug。

---

## 自检

- [x] 已检索 `docs/requirements/`，确认无重复文档（CFG-01/CFG-02 不覆盖编译期裁剪）
- [x] 本文 + `config/README.md` + `ROADMAP.md` 索引已挂接
- [x] `FEATURE_FLAGS.md` 剖面表已同步（默认剖面变更）
- [x] 代码改动未超出本文范围（仅剖面块 + CAN 收帧解耦）
- [x] 编译验证通过（`PROFILE_CAN_COMM` 与 `PROFILE_MOTOR` 均 build 成功，见 §2.2 实测）
