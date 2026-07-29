# I04 · Handle App 与键位映射

| 项 | 内容 |
|----|------|
| 原则 | 事件逻辑与业务分离；映射只在 App 内 |
| 状态 | 占位；**具体键位实现时再钉**（TBD） |

## 应用列表（v0.1）

| App | 宏（建议） | 默认 | 职责 |
|-----|------------|------|------|
| log | `DEEP_DOG_HANDLE_APP_LOG_ENABLE` | 开 | 打印快照 / 边沿，联调 |
| dog | `DEEP_DOG_HANDLE_APP_DOG_ENABLE` | 开（需 `DEEP_DOG_DOG_ENABLE`） | 摇杆/键 → 运控 |
| servo | `DEEP_DOG_HANDLE_APP_SERVO_ENABLE` | 关 | 舵机调试占位 |

Dispatcher **fan-out**：每个更新送给全部已注册 App；App 内过滤。无运行时 NVS 绑定（与 touch v0.1 一致）。

## 映射占位（TBD）

抽象按钮名见 YAML；与 Xbox / PS4 物理键对照在实现 PR 中定稿。

| 输入 | log | dog（建议方向） | servo |
|------|-----|-----------------|-------|
| 左摇杆 `lx/ly` | 打印 | 平移 / 前进后退（死区 TBD） | — |
| 右摇杆 `rx/ry` | 打印 | 转向或云台预留 | 预留 |
| `a` | 打印 | 站立或确认（TBD） | — |
| `b` | 打印 | 停止 / 趴下（TBD） | — |
| `x` / `y` | 打印 | TBD | TBD |
| `l1` / `r1` | 打印 | TBD | — |
| `l2` / `r2` | 打印 | 模拟量预留 | — |
| `start` | 打印 | `dog.init()` 类（TBD） | — |
| `select` | 打印 | TBD | — |

`handle/cmd` `disable` 时：dog/servo App **不执行**执行器调用；log 仍可打印。

## 与 touch App 关系

- 触摸与手柄可同时注册；若同时驱动狗，**后到的命令生效**（与多源策略一致），实现时避免互相抢状态机则加简单互斥或优先级（TBD，默认后到覆盖）。

## 验收

- [x] App 列表与宏命名写入需求
- [ ] 键位表在实现 PR 中从 TBD 改为确定值
- [ ] 与 [07-dog](../mqtt/modules/07-dog.md) 动作枚举对齐文档
