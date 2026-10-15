# MOT-05 · 轴归一化样例（电机）

| 项 | 内容 |
|----|------|
| ID | MOT-05 |
| 权威框架 | **[I08b-axis-mapping](../input/I08b-axis-mapping.md)**（本文仅样例） |
| catalog | [04-handle-motor-catalog](./04-handle-motor-catalog.md) |

## 样例：`rx → motor.pos_norm`

```text
手柄 rx ∈ [-1, 1]  （右为正）
死区后 u'
pos_rad = u' * P_MAX     # P_MAX = 12.57
```

| 摇杆 | u | 电机位置 |
|------|---|---------|
| 右满偏 | +1 | +12.57 rad |
| 左满偏 | −1 | −12.57 rad |
| 回中 | ~0 | 0 rad |

## 通用性

同一 `axis_bindings` 形状在 `gimbal` / `led_demo` 下仍上报；仅 catalog 不同。  
前端 `AxisKeymapPanel` 不绑定 motor 页。

## 验收

- [ ] `profile=motor` 且默认表：右摇杆控制位置如上
- [ ] 切到 `led_demo`：轴编辑器仍可见，绑定可为全 `none`
