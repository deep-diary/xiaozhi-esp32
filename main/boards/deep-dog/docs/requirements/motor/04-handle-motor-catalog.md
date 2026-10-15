# MOT-04 · Handle `profile=motor` catalog

| 项 | 内容 |
|----|------|
| ID | MOT-04 |
| 状态 | 拍板 |
| 映射框架 | [I08a](../input/I08a-keymap-mqtt-contract-draft.md)（离散）· [I08b](../input/I08b-axis-mapping.md)（轴） |
| 宿主 | `HandleAppKeyMap`（**非**独占 Motor App） |

## 目标

为 `profile=motor` 提供离散 + 连续动作 catalog 与出厂默认绑定。  
映射形状与其它 profile 相同；仅 catalog / 默认表不同。

## Catalog

| id | kind | value_domain | 说明 |
|----|------|--------------|------|
| `none` | both | — | 无动作 |
| `motor.enable` | edge | — | 初始化/使能当前 `motor_id` |
| `motor.disable` | edge | — | 失能 / reset |
| `motor.pos_zero` | edge | — | 位置参考 → 0 |
| `motor.nudge_pos` | edge | — | 位置 +Δ（默认 +0.2 rad） |
| `motor.nudge_neg` | edge | — | 位置 −Δ |
| `motor.pos_norm` | axis | signed | `pos = u * P_MAX`（±12.57）；回中→0 |
| `motor.vel_norm` | axis | signed | 限速占位：`limit = \|u\| * V_MAX`（可选） |

默认 `motor_id=1`（可经 axis/key params 或后续 MQTT 扩展覆盖）。

## 默认绑定（`set_profile=motor` / `reset_keymap`）

**离散**

| key | press | hold |
|-----|-------|------|
| `a` | `motor.enable` | `none` |
| `b` | `motor.disable` | `none` |
| `x` | `motor.pos_zero` | `none` |
| `y` | `none` | `none` |
| `dpad_right` | `motor.nudge_pos` | `none` |
| `dpad_left` | `motor.nudge_neg` | `none` |
| 其余 | `none` | `none` |

**轴**

| axis | id |
|------|-----|
| `rx` | `motor.pos_norm` |
| `lx` `ly` `ry` `l2` `r2` | `none` |

## 验收

- [ ] `set_profile=motor` 后 `handle/keymap` catalog 含上表
- [ ] A 使能、B 失能；`rx` 满偏≈±12.57、回中→0（见 [05](./05-analog-axis-sample.md)）
