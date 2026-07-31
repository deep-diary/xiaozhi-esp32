# I04 · Handle App 与键位映射

| 项 | 内容 |
|----|------|
| 原则 | 事件逻辑与业务分离；映射只在 App 内 |
| 状态 | 抽象键位已定稿（见 [I01](./I01-architecture.md)）；业务动作仍可微调 |
| 运行时映射 | 灯带类见 [I08a](./I08a-keymap-mqtt-contract-draft.md)（`profile` + keymap）；运控类仍编译期 |

## 应用列表

| App | 宏（建议） | 默认 | 职责 |
|-----|------------|------|------|
| log | `DEEP_DOG_HANDLE_APP_LOG_ENABLE` | 开 | 打印快照 / 边沿，联调（不受 profile 关闭） |
| dog | `DEEP_DOG_HANDLE_APP_DOG_ENABLE` | 开（需 `DEEP_DOG_DOG_ENABLE`） | 摇杆/键 → 运控；仅 `profile=dog` 且未 `disable` 时执行 |
| led_map | `DEEP_DOG_HANDLE_APP_LED_MAP_ENABLE` | 开（需 LED） | 离散键 → `LedStripControl`；表来自 NVS/MQTT；仅 `profile=led_demo` |
| servo | `DEEP_DOG_HANDLE_APP_SERVO_ENABLE` | 关 | 舵机调试占位（远期可加 `profile=servo`） |

Dispatcher **fan-out**：每个更新送给全部已注册 App；App 内按 **`profile` + `disable`** 过滤。

### `profile` = App 类剖面

与「灯带控制一类 / 电机（狗）控制一类」同义，见 [I08a §0](./I08a-keymap-mqtt-contract-draft.md)：

| profile | 生效 App 类 |
|---------|-------------|
| `led_demo` | `led_map` 执行；**dog 整 App 停** |
| `dog` | `dog` 执行；keymap 不触发 |
| `off` | 二者均不驱动执行器 |

出厂默认：`led_demo`（I08a 拍板）。

## 输入 → App（`profile=dog` 时）

抽象名见 [I01](./I01-architecture.md)；PS4 物理键由 PC 桥归一化，固件 App **只认抽象字段**。

| 输入 | log | dog | servo |
|------|-----|-----|-------|
| 左摇杆 `lx/ly` | 打印 | `ly<0` 前进 / `ly>0` 后退（死区见宏） | — |
| 右摇杆 `rx/ry` | 打印 | 转向或云台预留 | 预留 |
| `a`（✕ / A） | 打印 | 站立或确认（TBD） | — |
| `b`（○ / B） | 打印 | 停止运控 | — |
| `x`（□ / X） | 打印 | TBD | TBD |
| `y`（△ / Y） | 打印 | TBD | TBD |
| `l1` / `r1` | 打印 | TBD | — |
| `l2` / `r2` | 打印 | 模拟量预留 | — |
| `start`（Options） | 打印 | `dog.init()` | — |
| `select`（Share） | 打印 | TBD | — |

`profile=led_demo` 时：上表 dog 列不执行；`a/b/x/y/l1/r1` 由 `led_map` 按 keymap 表触发（见 I08a）。

`handle/cmd` `disable` 时：dog / led_map / servo **不执行**执行器；log 仍可打印。

## 与 touch App 关系

- 触摸与手柄可同时注册；若同时驱动狗，**后到的命令生效**（与多源策略一致）。
- touch 暂无运行时 keymap；与 handle `profile` 独立。

## 验收

- [x] App 列表与宏命名写入需求
- [x] 抽象键位表定稿（Xbox / PS4）
- [x] `profile` / `led_map` 与 I08a 对齐写入
- [ ] 与 [07-dog](../mqtt/modules/07-dog.md) 动作枚举继续对齐
- [ ] 固件落地 `HandleAppLedMap` + profile 门控
