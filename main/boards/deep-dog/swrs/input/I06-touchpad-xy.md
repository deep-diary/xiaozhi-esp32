# I06 · 触控板坐标扩展

| 项 | 内容 |
|----|------|
| 状态 | **桥 + 固件透传已实现**；前端画点 P1 |
| 范围 | PS4 DualShock 触控板 **手指 XY + 点击**；Xbox 无此硬件则字段缺省 |
| 前置 | [I03](./I03-source-pc-mqtt-bridge.md) · [I05](./I05-mqtt-message-examples.md) §0 |
| 非目标（本需求） | DualSense 完整多指手势、压力、灯条 |

## 背景

- 默认桥走 **pygame.Joystick**：只有 Touch **点击**（`buttons.touch`），**无 XY**。
- DS4 **HID report 内含触点坐标**；需 **hidapi** 解析，不能靠 pygame。
- 目标：可选扩展契约，不破坏现有 `a/b/x/y`、摇杆等核心字段。

## 目标行为

1. PC 桥在支持的平台上读取触控板：
   - `active`：至少一指按下  
   - `x`,`y`：主触点归一化坐标  
   - 点击仍映射 `buttons.touch`（与现网兼容）
2. 经 `handle/input` → 设备透传 → `handle/status` → 前端在 TOUCH 区画触点。
3. 无 hidapi / 非 DS4 / Xbox：不传 `touchpad` 字段；前端保持「仅点击或灰显」。

## 契约

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
| `touchpad.y` | float [0,1] | 上→下（与屏幕一致） |
| `touchpad.fingers` | int, optional | 触点数；v1 可只保证 0/1/2 计数 |
| `buttons.touch` | bool | **保留**：点击/按下（pygame 或 HID 派生） |

**双指**：v1 仍只报 **一对主触点** `x/y`（取 finger0）+ `fingers`；不扩展为两点数组。前端画一个圆点，`fingers>1` 旁注。

**不**把 XY 塞进 `axes.lx` 等，避免与摇杆语义冲突。

YAML / 固件 / 前端均按 **optional** 解析：旧端忽略未知字段。

## 实现分层

| 层 | 工作 | 状态 |
|----|------|------|
| 桥 | 默认 pygame；`--touchpad-xy` → **hidapi 全量读**同一份 DS4 report（按键+摇杆+XY） | done |
| 探测 | [`scripts/deep_dog_ds4_touchpad_probe.py`](../../../../../scripts/deep_dog_ds4_touchpad_probe.py)（`--mode hid\|hybrid`） | done |
| 共享解析 | [`scripts/deep_dog_ds4_hid.py`](../../../../../scripts/deep_dog_ds4_hid.py) | done |
| 契约 | `deep-dog-mqtt.yml` + [11-handle](../mqtt/modules/11-handle.md) / I05 样例 | done |
| 固件 | `HandleTouchpad` + `handle_mqtt` 透传 | done |
| 前端 | TOUCH 区域加大并画主触点圆点；无 `touchpad` 时仅 `buttons.touch` 高亮 | done（deep-trace HandleModulePanel） |
| deep-trace REQ-IOT-230 | 同步可选字段与 UI | done |

### 桥约束

- 默认 **关闭** XY（避免 hidapi 依赖与设备占用问题）；显式 `--touchpad-xy` 开启。
- 首版路径：**macOS + DS4**（USB report `0x01` 或 BT report `0x11`）；开启后走 **仅 HID 全量读**（不与 pygame 并行，避免抢设备）。
- macOS BT 注意：未正确 `write` 32 字节 `0x05` 唤醒时，系统只给 **10B GamePad** 短包（无触控、L2 会误读成 ~0.03）；桥内已强制唤醒并丢弃短包。
- 休眠/重连：与手柄自动重连同一策略；XY 源掉线时发 `touchpad.active=false`。
- 探测：先跑 probe `--mode hid` 验证按键与触控可同时解析；`--mode hybrid` 仅作抢占对照。

## 前端展示（需求）

- DualShock 示意中的 TOUCH 面板：
  - `touchpad.active`：显示触点，位置按 x/y  
  - 仅有 `buttons.touch`：整板高亮（现状）  
  - 皆无：灰显  
- 不要求多指轨迹；v1 单点即可。

## 验收

- [x] YAML 含可选 `touchpad`；旧 payload 仍合法  
- [x] 桥 `--touchpad-xy` 在 macOS+DS4 USB 路径上报 x/y（hidapi 全量读）  
- [x] 设备 status 原样透传  
- [ ] 前端可见触点移动（P1）  
- [x] Xbox / 无 HID：无字段、不报错（不开 flag 或非 DS4）  
- [x] 关闭 flag 时行为与现网一致（仅 `buttons.touch`）

## 明确不做（本轮）

- 用触控板驱动狗运动（除非另开 App 映射需求）  
- 强制所有用户安装 hidapi  
- 在 pygame 层「猜测」坐标  
