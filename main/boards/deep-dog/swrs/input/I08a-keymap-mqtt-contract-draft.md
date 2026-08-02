# I08a · handle keymap MQTT 契约

| 项 | 内容 |
|----|------|
| 状态 | **拍板**（`schema_ver` **4**） |
| 依据 | [I08 可行性评估](./I08-dynamic-key-action-mapping-eval.md) |
| 范围 | 离散 bool 键 **press + hold** → catalog；NVS；`profile` 切 App |
| 本轮 | `led_demo` / `gimbal` 执行；`dog`/`off` 不触发 keymap |
| 非目标 | 摇杆/`l2`/`r2` 模拟量 remap、宏连招 |
| 前端 | deep-trace handle 230 + 共用绑定组件；云台页复用 |

前缀：`deepdiary/deep-dog/{device_id}/`

---

## 0. `profile` ≈ App 类

| `profile` | 行为 |
|-----------|------|
| `led_demo` | keymap → `led.*`；dog 停 |
| `gimbal` | keymap → `gimbal.*`；dog 停 |
| `dog` | 编译期狗映射；keymap 不触发 |
| `off` | 均不驱动执行器 |

**`set_profile` 拍板**：切换时 **始终加载该应用的默认绑定表**（覆盖当前 bindings），再可选 persist。  
UI：先选应用 → 下拉只显示该应用 `catalog` → 表已是默认，可再改。

---

## 1. Topic

| Topic | 方向 | retain | 用途 |
|-------|------|--------|------|
| `handle/cmd` | ↓ | false | set_keymap / get_keymap / reset_keymap / set_profile |
| `handle/keymap` | ↑ | **true** | 当前表 + **按 profile 过滤的 catalog** + `bindable_keys` |

---

## 2. 可绑定按键（schema_ver 3）

| key | 说明 |
|-----|------|
| `a` `b` `x` `y` | 面键 |
| `l1` `r1` | 肩键 |
| `start` `select` | Options / Share |
| `dpad_up` `dpad_down` `dpad_left` `dpad_right` | 十字键（左手） |
| `l3` `r3` | 摇杆按下 |

不做：`l2`/`r2`、摇杆轴、`ps`/`touch`（预留）。

上行 `bindable_keys`：完整 key 名数组，供前端渲染行。

---

## 3. Catalog（按 profile 过滤）

| profile | catalog |
|---------|---------|
| `led_demo` | `none` + `led.*` |
| `gimbal` | `none` + `gimbal.*` |
| `dog` / `off` | 仅 `none` |

`gimbal.*`：`left|right|up|down|pan_speed_up|pan_speed_down|tilt_speed_up|tilt_speed_down`  
触发：`press`=单次（方向=nudge，调速=±档）；`hold`=jog 至松开。

---

## 4. Binding 形状

```json
{
  "a": {
    "press": { "id": "none" },
    "hold":  { "id": "gimbal.left" }
  }
}
```

兼容旧扁平 `{ "id": "led.static", ... }` ≡ press only。

---

## 5. 上行样例（profile=gimbal）

```json
{
  "schema_ver": 3,
  "ok": true,
  "profile": "gimbal",
  "persist": true,
  "source": "nvs",
  "bindable_keys": [
    "a", "b", "x", "y", "l1", "r1",
    "start", "select",
    "dpad_up", "dpad_down", "dpad_left", "dpad_right",
    "l3", "r3"
  ],
  "bindings": { "...": "见固件默认表" },
  "catalog": [
    "none",
    "gimbal.left", "gimbal.right", "gimbal.up", "gimbal.down",
    "gimbal.pan_speed_up", "gimbal.pan_speed_down",
    "gimbal.tilt_speed_up", "gimbal.tilt_speed_down"
  ],
  "warnings": [],
  "ts": 1710000000
}
```

---

## 6. 默认绑定（`set_profile` / `reset_keymap`）

**led_demo**：A 红 / B 蓝 static；X breathe；Y off（press）。  
**gimbal**：A/B/X/Y press+hold→左右上下；左侧十字键 press→调速（←/→ pan，↑/↓ tilt）。  
**dog / off**：全 `none`。

---

## 7. NVS

| 项 | 约定 |
|----|------|
| namespace | `h_keymap` |
| schema_ver | **4**（云台默认：面键方向 + 十字调速；旧 blob 失效 → 出厂默认） |

---

## 8. 验收

- [x] press/hold + gimbal profile
- [x] schema_ver 3 扩键 + catalog 过滤 + set_profile 加载默认（现行 **schema_ver 4**：面键方向 + 十字调速）
- [x] 实机 BT：`profile=gimbal` 下面键可触发 keymap（曾受 include 误钳 ENABLE=0，已修）
- [ ] 实机 PC 桥 DS4：`source=wifi` 下绑定与 jog（合入前联调）
- [ ] 其它 profile 默认表 / catalog 扩展（后续按需）
