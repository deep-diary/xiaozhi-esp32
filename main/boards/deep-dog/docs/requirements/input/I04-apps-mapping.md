# I04 · Handle App 与键位映射

| 项 | 内容 |
|----|------|
| 原则 | 事件逻辑与业务分离；**动态映射宿主为 KeyMap**（离散 + 轴） |
| 状态 | 抽象键位已定稿（见 [I01](./I01-architecture.md)）；业务动作仍可微调 |
| 运行时映射 | [I08a](./I08a-keymap-mqtt-contract-draft.md)（离散）· [I08b](./I08b-axis-mapping.md)（轴）；运控类 `dog` 仍编译期 |

## 应用列表

| App | 宏（建议） | 默认 | 职责 |
|-----|------------|------|------|
| log | `DEEP_DOG_HANDLE_APP_LOG_ENABLE` | 开 | 打印快照 / 边沿，联调（不受 profile 关闭） |
| dog | `DEEP_DOG_HANDLE_APP_DOG_ENABLE` | 开（需 `DEEP_DOG_DOG_ENABLE`） | 摇杆/键 → 运控；仅 `profile=dog` 且未 `disable` 时执行 |
| keymap | `DEEP_DOG_HANDLE_APP_KEYMAP_ENABLE` | 开 | 离散 press/hold **与**轴连续 → catalog；按 profile 过滤 |
| servo | `DEEP_DOG_HANDLE_APP_SERVO_ENABLE` | 关 | 舵机调试占位（远期可加 `profile=servo`） |

Dispatcher **fan-out**：每个更新送给全部已注册 App；App 内按 **`profile` + `disable`** 过滤。

**不**新增独占读轴的 `HandleAppMotor`；电机动作为 keymap catalog 的执行器回调。

### `profile` = App 类剖面

| profile | 生效 |
|---------|------|
| `led_demo` | keymap → `led.*`；dog 停 |
| `gimbal` | keymap → `gimbal.*`；dog 停 |
| `motor` | keymap → `motor.*`（离散+轴）；dog 停 |
| `dog` | `dog` 执行；keymap 不触发 |
| `off` | 均不驱动执行器 |

出厂默认：`led_demo`（I08a）；单电机联调时 Web/`set_profile` 切到 `motor`。

## 输入 → App

抽象名见 [I01](./I01-architecture.md)；固件 App **只认抽象字段**。

| 输入 | log | dog（profile=dog） | keymap（led/gimbal/motor） |
|------|-----|--------------------|----------------------------|
| 左摇杆 `lx/ly` | 打印 | `ly` 前进/后退（死区宏） | **axis_bindings**（I08b） |
| 右摇杆 `rx/ry` | 打印 | 转向预留 | **axis_bindings** |
| `l2`/`r2` | 打印 | 预留 | **axis_bindings** |
| `a/b/x/y/l1/r1` 等 | 打印 | 见 dog App | press/hold 表 |
| `b` | 打印 | 停止运控 | 见 keymap |
| `start` | 打印 | `dog.init()` | 见 keymap |

`handle/cmd` `disable` 时：dog / keymap **不执行**执行器；log 仍可打印。

## 与 touch App 关系

- 触摸与手柄可同时注册；若同时驱动狗，**后到的命令生效**。
- touch 暂无运行时 keymap；与 handle `profile` 独立。

## 验收

- [x] App 列表与宏命名写入需求
- [x] 抽象键位表定稿（Xbox / PS4）
- [x] `profile` / keymap / gimbal 与 I08a 对齐
- [x] 固件落地 `HandleAppKeyMap` + profile 门控
- [ ] `motor` profile + I08b 轴映射落地
- [ ] 与 [07-dog](../mqtt/modules/07-dog.md) 动作枚举继续对齐（后续运控联调）
