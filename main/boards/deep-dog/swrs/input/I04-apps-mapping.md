# I04 · Handle App 与键位映射

| 项 | 内容 |
|----|------|
| 原则 | 事件逻辑与业务分离；映射只在 App 内 |
| 状态 | 抽象键位已定稿（见 [I01](./I01-architecture.md)）；业务动作仍可微调 |
| 运行时映射 | [I08a](./I08a-keymap-mqtt-contract-draft.md)（`profile` + press/hold keymap）；运控类仍编译期 |

## 应用列表

| App | 宏（建议） | 默认 | 职责 |
|-----|------------|------|------|
| log | `DEEP_DOG_HANDLE_APP_LOG_ENABLE` | 开 | 打印快照 / 边沿，联调（不受 profile 关闭） |
| dog | `DEEP_DOG_HANDLE_APP_DOG_ENABLE` | 开（需 `DEEP_DOG_DOG_ENABLE`） | 摇杆/键 → 运控；仅 `profile=dog` 且未 `disable` 时执行 |
| keymap | `DEEP_DOG_HANDLE_APP_KEYMAP_ENABLE` | 开 | 离散键 press/hold → catalog 动作；`profile=gimbal` 执行云台；`led_demo` 预留灯带 |
| servo | `DEEP_DOG_HANDLE_APP_SERVO_ENABLE` | 关 | 舵机调试占位（远期可加 `profile=servo`） |

Dispatcher **fan-out**：每个更新送给全部已注册 App；App 内按 **`profile` + `disable`** 过滤。

### `profile` = App 类剖面

与「灯带 / 云台 / 电机（狗）」同义，见 [I08a §0](./I08a-keymap-mqtt-contract-draft.md)：

| profile | 生效 App 类 |
|---------|-------------|
| `led_demo` | keymap 触发 `led.*`（执行器可后续接齐）；**dog 整 App 停** |
| `gimbal` | keymap 触发 `gimbal.*`；dog 停 |
| `dog` | `dog` 执行；keymap 不触发 |
| `off` | 均不驱动执行器 |

出厂默认：`led_demo`（I08a）；云台联调时 Web/`set_profile` 切到 `gimbal`。

## 输入 → App（`profile=dog` 时）

抽象名见 [I01](./I01-architecture.md)；PS4 物理键由 PC 桥归一化，固件 App **只认抽象字段**。

| 输入 | log | dog | keymap（非 dog） |
|------|-----|-----|------------------|
| 左摇杆 `lx/ly` | 打印 | `ly<0` 前进 / `ly>0` 后退（死区见宏） | — |
| 右摇杆 `rx/ry` | 打印 | 转向预留 | — |
| `a/b/x/y/l1/r1` | 打印 | 见下 | press/hold 按表 |
| `b` | 打印 | 停止运控 | 见 keymap |
| `start` | 打印 | `dog.init()` | — |
| `l2`/`r2` / 摇杆 | 打印 | 模拟量预留 | **不做** remap |

`profile=gimbal` 时：dog 列不执行；`a/b/x/y/l1/r1` 由 keymap 按 press/hold 表触发 `gimbal.*`。

`handle/cmd` `disable` 时：dog / keymap **不执行**执行器；log 仍可打印。

## 与 touch App 关系

- 触摸与手柄可同时注册；若同时驱动狗，**后到的命令生效**（与多源策略一致）。
- touch 暂无运行时 keymap；与 handle `profile` 独立。

## 验收

- [x] App 列表与宏命名写入需求
- [x] 抽象键位表定稿（Xbox / PS4）
- [x] `profile` / keymap / gimbal 与 I08a 对齐写入
- [x] 固件落地 `HandleAppKeyMap` + profile 门控（`led_demo` / `gimbal` / `dog` / `off`）
- [ ] 与 [07-dog](../mqtt/modules/07-dog.md) 动作枚举继续对齐（后续运控联调）
- [ ] 其它 App 类默认绑定表扩展（用到时再补，不必本轮）
