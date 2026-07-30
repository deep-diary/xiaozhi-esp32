# I06 · 触控板坐标扩展

| 项 | 内容 |
|----|------|
| 状态 | **桥 + 固件透传已实现**；双点 `contacts` + 前端画两点 |
| 范围 | PS4 DualShock 触控板 **最多 2 指 XY + 点击**；Xbox 无此硬件则字段缺省 |
| 前置 | [I03](./I03-source-pc-mqtt-bridge.md) · [I05](./I05-mqtt-message-examples.md) §0 |
| 相关 | 手柄 IMU 见 [I07](./I07-motion-gyro.md)；手势识别 → I08 |
| 非目标（本需求） | DualSense 完整多指手势、压力、灯条；swipe/pinch 枚举 |

## 背景

- 默认桥走 **pygame.Joystick**：只有 Touch **点击**（`buttons.touch`），**无 XY**。
- DS4 **HID report 含 2 个触点槽**；需 **hidapi** 解析。
- v1 曾只报主触点一对 `x/y`；现扩展 **`contacts[]`（0～2）**，保留 `x/y` = contacts[0] 兼容旧前端。

## 目标行为

1. PC 桥读取触控板：
   - `active`：至少一指按下  
   - `x`,`y`：主触点（finger0，若无则 finger1）归一化坐标  
   - `contacts`：最多 2 项 `{active,x,y}`  
   - `fingers`：活跃触点数 0～2  
   - 点击仍映射 `buttons.touch`
2. 经 `handle/input` → 设备透传 → `handle/status` → 前端画 **最多 2 个圆点**。
3. 无 hidapi / 非 DS4 / Xbox：不传 `touchpad`；前端「仅点击或灰显」。

## 契约

```json
{
  "touchpad": {
    "active": true,
    "x": 0.42,
    "y": 0.55,
    "fingers": 2,
    "contacts": [
      { "active": true, "x": 0.42, "y": 0.55 },
      { "active": true, "x": 0.71, "y": 0.48 }
    ]
  }
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `touchpad` | object, optional | 缺省 = 不支持或未启用 |
| `touchpad.active` | bool | 有触点 |
| `touchpad.x` / `y` | float [0,1] | **兼容**：主触点（= contacts[0] 活跃点） |
| `touchpad.fingers` | int | 0～2 |
| `touchpad.contacts` | array, optional | 最多 2 项；缺省则仅用 x/y |
| `contacts[].active` | bool | 该指是否按下 |
| `contacts[].x` / `y` | float [0,1] | 左→右 / 上→下 |
| `buttons.touch` | bool | 点击（保留） |

**不**把 XY 塞进 `axes.lx` 等。YAML / 固件 / 前端均按 **optional** 解析。

## 实现分层

| 层 | 工作 | 状态 |
|----|------|------|
| 桥 | `--touchpad-xy` → hidapi；填 contacts + 仍填 x/y | done / 本轮双点 |
| 共享解析 | [`scripts/deep_dog_ds4_hid.py`](../../../../../scripts/deep_dog_ds4_hid.py) | done |
| 契约 | YAML + [11-handle](../mqtt/modules/11-handle.md) / I05 | 本轮更新 |
| 固件 | `HandleTouchpad` + contacts 透传 | 本轮 |
| 前端 | TOUCH 画最多 2 点 | 需求同步 |
| motion | 同 HID 路径见 [I07](./I07-motion-gyro.md) | 本轮 |

### 桥约束

- 默认关闭 XY；显式 `--touchpad-xy`。
- 开启后 **仅 HID 全量读**（含 motion）；不与 pygame 并行。
- macOS BT：须 `0x05` 唤醒全量 report。

## 前端展示

- `contacts` 有多项：每个 `active` 触点画一圆点（可区分色/序号）
- 无 `contacts` 仅有 `x/y`：画单点（旧 payload）
- 仅有 `buttons.touch`：整板高亮
- 皆无：灰显

## 验收

- [x] YAML 含可选 `touchpad` / `contacts`
- [x] 桥上报双点 contacts
- [x] 固件 contacts 透传
- [x] 前端需求 REQ-IOT-230 双点显示
- [x] 设备 status 透传
- [ ] 前端可见两点（P1）
- [x] Xbox / 无 HID：无字段、不报错

## 明确不做

- 用触控板驱动狗运动（除非另开 App）
- 强制安装 hidapi
- 本轮不上报 swipe/pinch 手势枚举（I08）
