# I08b · 通用模拟量（轴）映射契约

| 项 | 内容 |
|----|------|
| 状态 | **拍板**（与 I08a 合并上行：`schema_ver` **7**） |
| 依据 | [I08a](./I08a-keymap-mqtt-contract-draft.md) · [I08 评估](./I08-dynamic-key-action-mapping-eval.md) |
| 范围 | `lx|ly|rx|ry|l2|r2` → catalog 连续动作；跨 **所有** keymap profile |
| 宿主 | `HandleAppKeyMap` + `keymap_store`（**非**某业务独占 App） |
| 样例 | [motor/05](../motor/05-analog-axis-sample.md) · catalog [motor/04](../motor/04-handle-motor-catalog.md) |
| 前端 | `AxisKeymapPanel` + `DiscreteKeymapPanel` → `HandleKeymapEditor` |

前缀：`deepdiary/deep-dog/{device_id}/`

---

## 0. 与 I08a 的关系

| 层 | I08a | I08b |
|----|------|------|
| 输入 | 离散 bool 键 | 模拟轴 / 扳机 |
| 触发 | 边沿 press / hold | **每帧**连续（快照更新时） |
| 存储 | `bindings` | `axis_bindings` |
| catalog | 同表；项带 `kind` | 同左 |
| profile | 过滤 catalog + 加载默认 | **相同规则**，另加载默认轴表 |

`dog` / `off`：keymap **不触发**执行器（含轴）；表仍可读写。

---

## 1. 可绑定轴

| axis | 域 | 说明 |
|------|-----|------|
| `lx` `ly` `rx` `ry` | signed `[-1,1]` | 右/下为正（与 handle status 一致） |
| `l2` `r2` | unit `[0,1]` | 扳机 |

上行 `bindable_axes`：固定完整列表，供前端渲染。

---

## 2. Catalog 元数据

上行 `catalog` 可为字符串数组（兼容），或对象数组（推荐）：

```json
{
  "id": "motor.pos_norm",
  "kind": "axis",
  "value_domain": "signed"
}
```

| `kind` | 含义 |
|--------|------|
| `edge` | 仅离散 press/hold |
| `axis` | 仅连续轴 |
| `both` | 两者皆可（如 `none`） |

| `value_domain` | 含义 |
|----------------|------|
| `signed` | 固件钳 `u∈[-1,1]` |
| `unit` | 固件钳 `u∈[0,1]` |
| 省略 | `none` 或不连续动作 |

前端：`DiscreteKeymapPanel` 过滤 `kind∈{edge,both}`；`AxisKeymapPanel` 过滤 `kind∈{axis,both}`。

---

## 3. `axis_bindings` 形状

```json
{
  "rx": { "id": "motor.pos_norm", "motor_id": 1 },
  "ly": { "id": "none" }
}
```

可选 params（稀疏）：`motor_id`、`scale`（默认 1）、`deadzone`（缺省用固件宏）。

未知 `id` → 忽略并 `warnings`；不写 Flash。

---

## 4. 运行时

```text
OnSnapshot:
  for each axis in bindable_axes:
    u = raw axis / trigger
    u' = ApplyDeadzone(u)
    act = axis_bindings[axis]
    if act.id != none:
      FireContinuous(act, u')
```

归一化模板在**执行器**侧：`physical = u' * span * scale`（例 `motor.pos_norm`：`span=P_MAX`）。

---

## 5. `set_profile` / 默认轴表

切换 profile **始终**加载该应用默认 `bindings` **与** `axis_bindings`（覆盖当前），再可选 persist。

| profile | 默认轴（摘要） |
|---------|----------------|
| `motor` | `rx→motor.pos_norm`；其余 `none`（见 motor/04） |
| `gimbal` | `rx→gimbal.pan_rate`，`ry→gimbal.tilt_rate`；`r3.press→gimbal.home`；`lx`/`ly`/`l2`/`r2`→`none` |
| `led_demo` / `dog` / `off` | 全 `none` |

云台连续动作（`kind=axis`，`value_domain=signed`）：

| id | 语义 |
|----|------|
| `gimbal.pan_rate` | `u∈[-1,1]` → 水平速率；`u>0` 右、`u<0` 左；回中停该轴 |
| `gimbal.tilt_rate` | `u∈[-1,1]` → 垂直速率；`u>0` 下、`u<0` 上 |

`l2`/`r2`（unit `[0,1]`）本轮默认 `none`，须出现在 `axis_bindings` 与前端轴面板，供后续无负量业务绑定。

---

## 6. 上行样例增量（schema_ver 5）

```json
{
  "schema_ver": 5,
  "profile": "motor",
  "bindable_keys": ["a", "b", "x", "y", "l1", "r1", "start", "select",
                    "dpad_up", "dpad_down", "dpad_left", "dpad_right", "l3", "r3"],
  "bindable_axes": ["lx", "ly", "rx", "ry", "l2", "r2"],
  "bindings": {},
  "axis_bindings": {
    "rx": { "id": "motor.pos_norm" },
    "lx": { "id": "none" },
    "ly": { "id": "none" },
    "ry": { "id": "none" },
    "l2": { "id": "none" },
    "r2": { "id": "none" }
  },
  "catalog": [
    { "id": "none", "kind": "both" },
    { "id": "motor.enable", "kind": "edge" },
    { "id": "motor.pos_norm", "kind": "axis", "value_domain": "signed" }
  ],
  "ts": 1710000000
}
```

`set_keymap` 可稀疏更新 `axis_bindings`（`merge` 默认 true，与离散一致）。

---

## 7. NVS

| 项 | 约定 |
|----|------|
| namespace | `h_keymap`（与 I08a 同） |
| schema_ver | **7**（云台剖面出厂 `profile=gimbal` + 默认轴；旧 blob 失效） |

---

## 8. 前端组件

| 组件 | 职责 |
|------|------|
| `DiscreteKeymapPanel` | 现有 press/hold |
| `AxisKeymapPanel` | 轴 × 连续 action |
| `HandleKeymapEditor` | profile 顶栏 + 组合两面板；共用 `handle/keymap` / `set_keymap` |

电机详情页**不**重复轴编辑器；链到 handle 或嵌入同一容器。

---

## 9. 验收

- [x] `handle/keymap` 含 `bindable_axes` + `axis_bindings`，`schema_ver=5`
- [x] `profile=motor` 默认 `rx→motor.pos_norm` 实机生效
- [ ] `profile=gimbal` 默认 `rx/ry→pan_rate/tilt_rate`；斜推双轴同动
- [ ] 轴面板六轴（含 `l2`/`r2` 标注 0～1）可编辑并 persist
- [ ] 切 `led_demo`：轴面板仍渲染，catalog 按 profile 过滤
- [ ] `dog`/`off`：轴不驱动执行器
