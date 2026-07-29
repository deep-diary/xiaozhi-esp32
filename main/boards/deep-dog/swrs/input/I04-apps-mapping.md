# I04 · Handle App 与键位映射

| 项 | 内容 |
|----|------|
| 原则 | 事件逻辑与业务分离；映射只在 App 内 |
| 状态 | 抽象键位已定稿（见 [I01](./I01-architecture.md)）；业务动作仍可微调 |

## 应用列表（v0.1）

| App | 宏（建议） | 默认 | 职责 |
|-----|------------|------|------|
| log | `DEEP_DOG_HANDLE_APP_LOG_ENABLE` | 开 | 打印快照 / 边沿，联调 |
| dog | `DEEP_DOG_HANDLE_APP_DOG_ENABLE` | 开（需 `DEEP_DOG_DOG_ENABLE`） | 摇杆/键 → 运控 |
| servo | `DEEP_DOG_HANDLE_APP_SERVO_ENABLE` | 关 | 舵机调试占位 |

Dispatcher **fan-out**：每个更新送给全部已注册 App；App 内过滤。无运行时 NVS 绑定（与 touch v0.1 一致）。

## 输入 → App（v0.1）

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

`handle/cmd` `disable` 时：dog/servo App **不执行**执行器调用；log 仍可打印。

## 与 touch App 关系

- 触摸与手柄可同时注册；若同时驱动狗，**后到的命令生效**（与多源策略一致）。

## 验收

- [x] App 列表与宏命名写入需求
- [x] 抽象键位表定稿（Xbox / PS4）
- [ ] 与 [07-dog](../mqtt/modules/07-dog.md) 动作枚举继续对齐
