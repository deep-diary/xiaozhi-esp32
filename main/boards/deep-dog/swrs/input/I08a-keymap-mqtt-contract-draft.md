# I08a · handle keymap MQTT 契约

| 项 | 内容 |
|----|------|
| 状态 | **拍板**（已合入 [`deep-dog-mqtt.yml`](../mqtt/protocol/deep-dog-mqtt.yml)；固件实现 planned） |
| 依据 | [I08 可行性评估](./I08-dynamic-key-action-mapping-eval.md) |
| 范围 v0 | 离散按键 **press** → **灯带动作**；NVS 可固化；`profile` 切换 App 类 |
| 非目标 v0 | 摇杆曲线、宏连招、狗运控 remap、任意脚本 |
| 模块归属 | `handle`（映射表 + profile）+ 执行仍走 `LedStripControl`（与 `led/cmd` 共用） |
| 前端 | deep-trace `docs/requirements/features/iot/modules/handle/230-module-handle.md` |

前缀：`deepdiary/deep-dog/{device_id}/`

---

## 0. 概念：`profile` ≈ App 类（剖面）

与 [I01](./I01-architecture.md) / [I04](./I04-apps-mapping.md) 的 **HandleApp** 同构：Hub 仍 fan-out，**哪一类业务 App 真正驱动执行器**由 `profile` 决定。

| `profile` | 对应 App 类 | 行为（拍板） |
|-----------|-------------|--------------|
| `led_demo` | 灯带类（`HandleAppLedMap`） | keymap 按键触发灯效；**dog App 整 App 停**（不驱动电机） |
| `dog` | 运控类（`HandleAppDog`） | 关闭 keymap 触发；恢复 I04 编译期狗映射 |
| `off` | 无执行类 | keymap 与 dog 均不驱动执行器；status / keymap 仍可上报 |

后续若加「舵机调试一类」，可再增 `servo` 等 profile，语义仍是：**一次只激活一类业务 App**（log 联调 App 可始终开）。

正交门控：

- `handle/cmd` `disable`：**所有**本地 HandleApps 不驱动执行器（优先于 profile）。
- `enable`：恢复后仍按当前 `profile` 决定哪一类执行。

出厂默认（拍板）：`profile=led_demo`（演示优先；量产若改 `dog` 用编译宏覆盖即可）。

---

## 1. Topic 一览

| Topic | 方向 | QoS | retain | 用途 |
|-------|------|-----|--------|------|
| `handle/cmd` | ↓ | 1 | false | 扩 `action`：改/查/重置 keymap、切 profile |
| `handle/keymap` | ↑ | 0 | **true** | 当前生效映射表 + profile（晚进页可直接拿到） |
| `handle/status` | ↑ | 0 | false | **不变**（轴/键快照；不含 bindings） |
| `led/cmd` · `led/status` | ↓↑ | 既有 | 既有 | 网页直接改灯效；与 keymap **并列** |

取舍（拍板）：

- 下行扩既有 **`handle/cmd`**，不新开 `handle/keymap/cmd`。
- 映射表用独立 retain **`handle/keymap`**，不塞进高频 status。

---

## 2. `handle/cmd` 扩展

在现有 `enable | disable | pair | rumble | output` 上增加：

| `action` | 说明 |
|----------|------|
| `set_keymap` | 稀疏或全量写入 bindings；可选 persist |
| `get_keymap` | 立刻重发一帧 `handle/keymap`（不改表） |
| `reset_keymap` | 恢复出厂默认 bindings；可选 persist |
| `set_profile` | 切换 App 类：`led_demo` / `dog` / `off` |

### 2.1 `set_keymap`

```json
{
  "action": "set_keymap",
  "merge": true,
  "persist": true,
  "bindings": {
    "a": { "id": "led.static", "r": 255, "g": 0, "b": 0, "brightness": 64 },
    "b": { "id": "led.static", "r": 0, "g": 0, "b": 255, "brightness": 64 }
  },
  "ts": 1710000000
}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `action` | string | 是 | `set_keymap` |
| `bindings` | object | 是 | key → binding；key 见 §3 |
| `merge` | bool | 否 | 默认 `true`：只改给出的键；`false`：未出现的键清为 `none` |
| `persist` | bool | 否 | 默认 `true`：写 NVS；`false`：仅 RAM |
| `ts` | int | 否 | Unix 秒 |

**对换 A/B**：

```json
{
  "action": "set_keymap",
  "merge": true,
  "persist": true,
  "bindings": {
    "a": { "id": "led.static", "r": 0, "g": 0, "b": 255, "brightness": 64 },
    "b": { "id": "led.static", "r": 255, "g": 0, "b": 0, "brightness": 64 }
  }
}
```

**单键清空**：

```json
{
  "action": "set_keymap",
  "bindings": { "x": { "id": "none" } },
  "persist": true
}
```

### 2.2 `get_keymap` / `reset_keymap`

```json
{ "action": "get_keymap", "ts": 1710000000 }
```

```json
{ "action": "reset_keymap", "persist": true, "ts": 1710000000 }
```

### 2.3 `set_profile`

```json
{ "action": "set_profile", "profile": "led_demo", "persist": true, "ts": 1710000000 }
```

| 字段 | 说明 |
|------|------|
| `profile` | `led_demo` \| `dog` \| `off`（见 §0） |
| `persist` | 默认 `true`：与 bindings 一并写入 NVS |

---

## 3. 可绑定按键（v0 · 拍板）

与 `HandleSnapshot.buttons` 对齐；**仅 bool 键 press 边沿**触发一次。

| key | v0 | 触发 |
|-----|----|------|
| `a` `b` `x` `y` | **必做** | press |
| `l1` `r1` | **必做** | press |
| `start` `select` | 不做（预留） | — |
| `dpad_*` / `l3` `r3` `ps` `touch` | 不做 | — |
| `l2` `r2` / 摇杆 | **不做** | 连续量仅在 `profile=dog` 时由 dog App 消费 |

未知 key → 忽略该条，并在 `handle/keymap.warnings` 回报。

---

## 4. 动作目录 `id`（v0 · 仅 LED）

| `id` | 对应执行 | 参数（可选；缺省用当前灯态或出厂默认） |
|------|----------|----------------------------------------|
| `none` | 无操作 | — |
| `led.off` | `ApplyOff` | — |
| `led.static` | `ApplyStatic` | `r,g,b` · `brightness` |
| `led.blink` | `ApplyBlink` | `r,g,b` · `brightness` · `interval_ms` |
| `led.breathe` | `ApplyBreathe` | 高/低色 · 亮度 · `interval_ms` |
| `led.scroll` | `ApplyScroll` | 同上 + `scroll_length` |
| `led.system` | `ApplySystem` | 交还应用绑定（mode=5） |

约定（拍板）：

- MQTT 上 `id` 用 **点分字符串**；固件内映到枚举。
- 参数名与 [`led/cmd`](../mqtt/modules/08-led.md) **同名同范围**。
- 未知 `id` → 该键当 `none`；`ok` 可为 false 并带 `warnings`。

**v1 预留**：`dog.stand` / `dog.stop` / `servo.nudge` …（届时再扩 catalog，不改 Topic）。

### Binding 对象

```json
{
  "id": "led.blink",
  "r": 255, "g": 40, "b": 0,
  "brightness": 80,
  "interval_ms": 300
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `id` | string | 上表 |
| `r` `g` `b` | int 0–255 | 主色 |
| `low_r` `low_g` `low_b` | int 0–255 | 低色 |
| `brightness` `low_brightness` | int 0–255 | 亮度 |
| `interval_ms` | int | 动画间隔 |
| `scroll_length` | int | 滚动长度 |

---

## 5. 上行 `handle/keymap`（retain）

PUBLISH 时机：上电加载 NVS/默认后；`set_keymap` / `reset_keymap` / `set_profile` 后；`get_keymap`；MQTT 重连后。

`catalog`：**每次** keymap 报文都带（拍板），供前端下拉，无需另扩 `device/info`。

```json
{
  "schema_ver": 1,
  "ok": true,
  "profile": "led_demo",
  "persist": true,
  "source": "nvs",
  "bindings": {
    "a": { "id": "led.static", "r": 255, "g": 0, "b": 0, "brightness": 64 },
    "b": { "id": "led.static", "r": 0, "g": 0, "b": 255, "brightness": 64 },
    "x": { "id": "led.breathe", "r": 0, "g": 64, "b": 0, "low_r": 0, "low_g": 8, "low_b": 0, "interval_ms": 50 },
    "y": { "id": "led.off" },
    "l1": { "id": "none" },
    "r1": { "id": "none" }
  },
  "catalog": [
    "none",
    "led.off",
    "led.static",
    "led.blink",
    "led.breathe",
    "led.scroll",
    "led.system"
  ],
  "warnings": [],
  "ts": 1710000000
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `schema_ver` | int | 表结构版本 |
| `ok` | bool | 校验是否通过 |
| `profile` | string | 当前 App 类剖面 |
| `persist` | bool | RAM 是否已与 NVS 一致 |
| `source` | string | `nvs` \| `default` \| `mqtt` |
| `bindings` | object | 至少含 `a,b,x,y,l1,r1` |
| `catalog` | string[] | 本固件支持的 `id` |
| `warnings` | string[] | 可选 |
| `ts` | int | Unix 秒 |

---

## 6. 与 `led/cmd` 的控制权

| 来源 | 行为 |
|------|------|
| 按键经 keymap（且 `profile=led_demo`） | `LedStripControl::Apply*` → 推 `led/status` |
| `led/cmd` / MCP | 直接改灯效；**不改** keymap 表 |
| 之后再按键 | 再按 keymap 覆盖灯效 |
| `led/cmd` mode=5 | 交还应用绑定；keymap 再触发可抢回 |

**keymap = 触发器；`led/cmd` = 直接旋钮**；最后写入者生效。

---

## 7. NVS

| 项 | 约定 |
|----|------|
| namespace | `h_keymap` |
| 内容 | `schema_ver` + `profile` + bindings |
| 校验 | `id` ∈ catalog；颜色 clamp；失败 → 出厂默认，`source=default` |
| 出厂默认 | A 红 static、B 蓝 static；`x/y/l1/r1` = `none`；`profile=led_demo` |

---

## 8. 拍板纪要（原 §11）

| # | 决议 |
|---|------|
| 1 | 下行扩 `handle/cmd`（不新开 keymap/cmd） |
| 2 | 出厂默认 `profile=led_demo` |
| 3 | `led_demo` 时 **dog App 整停** |
| 4 | `catalog` 每次 `handle/keymap` 都带 |
| 5 | binding.`id` 用 **字符串** |
| 6 | v0 键集 = **abxy + l1/r1** |
| — | **`profile` = App 类剖面**（灯带类 / 运控类 / 关闭） |

---

## 9. 前端 Steps

1. 校验 `capabilities.handle`（改灯效绑定可再要求 `capabilities.led`）。
2. 订阅 retain `handle/keymap`；`catalog` → 动作下拉；展示当前 `profile`。
3. UI：profile 切换（灯带演示 / 运控 / 关闭）+ 六键（a/b/x/y/l1/r1）绑定编辑。
4. `set_keymap`（`persist: true`）→ 等 keymap 回读；`set_profile` 同理。
5. `led_demo` 下用真手柄或桥按键验收灯效。
6. unmount 退订 keymap（status 按原 handle 页）。

---

## 10. 联调时序（对换 A/B）

```text
Web                          Device                         LED / Motor
 │  set_profile led_demo      │                              │
 │ ─────────────────────────► │ dog App 停；LedMap 开         │
 │  handle/keymap             │                              │
 │ ◄───────────────────────── │                              │
 │  set_keymap A↔B persist    │ NVS                          │
 │ ─────────────────────────► │                              │
 │  handle/keymap 新表        │                              │
 │ ◄───────────────────────── │                              │
 │                            │ 按 A → ApplyStatic(蓝) ─────►│ LED
 │  led/status                │                              │
 │ ◄───────────────────────── │                              │
```

---

## 11. 验收

- [x] 契约拍板；`profile` 语义对齐 HandleApp 类
- [x] 合入 YAML + [11-handle](../mqtt/modules/11-handle.md)
- [x] 同步 deep-trace 230
- [ ] 固件：`HandleAppLedMap` + NVS + cmd 处理（planned）
