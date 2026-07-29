# I06 · 触控板坐标扩展（planned）

| 项 | 内容 |
|----|------|
| 状态 | **需求已写；实现未做** |
| 范围 | PS4 DualShock 触控板 **手指 XY + 点击**；Xbox 无此硬件则字段缺省 |
| 前置 | [I03](./I03-source-pc-mqtt-bridge.md) · [I05](./I05-mqtt-message-examples.md) §0 |
| 非目标（本需求） | DualSense 完整多指手势、压力、灯条 |

## 背景

- 当前桥走 **pygame.Joystick**：只有 Touch **点击**（`buttons.touch`），**无 XY**。
- DS4 **HID report 内含触点坐标**；需 **hidapi（或等价）** 解析，不能靠 pygame。
- 目标：可选扩展契约，不破坏现有 `a/b/x/y`、摇杆等核心字段。

## 目标行为

1. PC 桥在支持的平台上读取触控板：
   - `active`：至少一指按下  
   - `x`,`y`：主触点归一化坐标  
   - 点击仍映射 `buttons.touch`（与现网兼容）
2. 经 `handle/input` → 设备透传 → `handle/status` → 前端在 TOUCH 区画触点。
3. 无 hidapi / 非 DS4 / Xbox：不传 `touchpad` 字段；前端保持「仅点击或灰显」。

## 契约（建议）

`handle/input` 与 `handle/status` **同构**增加可选对象：

```json
{
  "connected": true,
  "source": "wifi",
  "axes": { "lx": 0, "ly": 0, "rx": 0, "ry": 0 },
  "buttons": {
    "a": false, "b": false, "x": false, "y": false,
    "l1": false, "r1": false, "l2": 0, "r2": 0,
    "start": false, "select": false,
    "touch": true
  },
  "touchpad": {
    "active": true,
    "x": 0.42,
    "y": 0.55,
    "fingers": 1
  },
  "ts": 1710000000
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `touchpad` | object, optional | 缺省 = 本源不支持或未启用 |
| `touchpad.active` | bool | 有触点 |
| `touchpad.x` | float [0,1] | 左→右 |
| `touchpad.y` | float [0,1] | 上→下（与屏幕一致；实现时钉死并写进 I05 样例） |
| `touchpad.fingers` | int, optional | 触点数；v1 可只保证 0/1 |
| `buttons.touch` | bool | **保留**：点击/按下（pygame 或 HID 派生） |

**不**把 XY 塞进 `axes.lx` 等，避免与摇杆语义冲突。

YAML / 固件 / 前端均按 **optional** 解析：旧端忽略未知字段。

## 实现分层

| 层 | 工作 | 优先级 |
|----|------|--------|
| 桥 | `pygame` 继续管键鼠摇杆；可选 `--touchpad-xy` 用 **hidapi** 读 DS4 report 触控段 | P0 |
| 契约 | `deep-dog-mqtt.yml` 增加 `touchpad`；[11-handle](../mqtt/modules/11-handle.md) / I05 样例 | P0 |
| 固件 | `HandleSnapshot` + `handle_mqtt` 透传 `touchpad`（与 dpad 扩展同类） | P0 |
| 前端 | TOUCH 区域画圆点；无 `touchpad` 时仅 `buttons.touch` 高亮 | P1 |
| deep-trace REQ-IOT-230 | 同步可选字段与 UI | P1 |

### 桥约束（需求级）

- 默认 **关闭** XY（避免 hidapi 依赖与设备占用问题）；显式 flag 开启。
- 首版只保证：**macOS + DS4 USB**（或已验证的一条路径）；BT / Windows / DualSense 单列验收。
- 若 hidapi 与 pygame 抢设备：允许「仅 HID 全量读」备选模式（需求允许二选一实现，文档写清）。
- 休眠/重连：与手柄自动重连同一策略；XY 源掉线时清 `touchpad.active=false`。

## 前端展示（需求）

- DualShock 示意中的 TOUCH 面板：
  - `touchpad.active`：显示触点，位置按 x/y  
  - 仅有 `buttons.touch`：整板高亮（现状）  
  - 皆无：灰显  
- 不要求多指轨迹；v1 单点即可。

## 验收

- [ ] YAML 含可选 `touchpad`；旧 payload 仍合法  
- [ ] 桥 `--touchpad-xy`（名称可定）在至少一种 OS+DS4 组合上报 x/y  
- [ ] 设备 status 原样透传；前端可见触点移动  
- [ ] Xbox / 无 HID：无字段、不报错  
- [ ] 关闭 flag 时行为与现网一致（仅 `buttons.touch`）

## 明确不做（本轮）

- 用触控板驱动狗运动（除非另开 App 映射需求）  
- 强制所有用户安装 hidapi  
- 在 pygame 层「猜测」坐标  
