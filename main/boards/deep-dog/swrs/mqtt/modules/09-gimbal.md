# 09 · gimbal（云台）

| 项 | 内容 |
|----|------|
| module_id | `gimbal` |
| capabilities | `gimbal` |
| 路由建议 | `/device/:deviceId/modules/gimbal` |
| 契约 | **ready**（含 action / 速度 / jog） |
| YAML | `gimbal/cmd`、`gimbal/status` |
| 驱动 | [`gimbal/`](../../../gimbal/) |
| 路线图 | **V-C04**（原 M03） |
| 参考 | [servo](./10-servo.md)、[I08a keymap](../../input/I08a-keymap-mqtt-contract-draft.md) |

## 入口卡文案

- 标题：云台  
- 说明：双轴 pan / tilt · 速度 · 手柄绑定  

## 详情页目标

1. 回读角度、轴速度、步进、限位、moving、ready  
2. Web 下发 absolute / relative / nudge / jog / 调速 / set_speed  
3. 手柄按键 **press / hold** 绑定云台动作（经 `handle/keymap`，`profile=gimbal`）

## Topic

| Topic | 方向 | QoS | retain |
|-------|------|-----|--------|
| `gimbal/status` | ↑ | 0 | true |
| `gimbal/cmd` | ↓ | 1 | false |

手柄绑定 Topic 见 [11-handle](./11-handle.md) / I08a（`handle/keymap` · `handle/cmd`）。

## 功能函数（固件 API）

| 函数语义 | MQTT `action` / 说明 |
|----------|----------------------|
| 向左 / 右 / 上 / 下（单步） | `nudge_left` … `nudge_down`；keymap `press` → 同语义 |
| 向左 / 右 / 上 / 下（连动） | `jog_start` + `dir` / `jog_stop`；keymap `hold` → 同语义 |
| 提高 / 降低水平速度 | `pan_speed_up` / `pan_speed_down` |
| 提高 / 降低垂直速度 | `tilt_speed_up` / `tilt_speed_down` |
| 设置运行速度 | `set_speed` + `pan_speed` / `tilt_speed`（°/s） |
| 停止 | `stop` |
| 绝对 / 相对角 | `mode: absolute\|relative` + `pan`/`tilt`；可选 `speed`（°/s，0=尽快） |

速度单位：**度/秒**。点按调速按档位步进（见 `gimbal_config.h`）。

## 样例 JSON

**cmd · 绝对角**

```json
{ "mode": "absolute", "pan": 135, "tilt": 90, "speed": 45, "ts": 1710000000 }
```

**cmd · jog**

```json
{ "action": "jog_start", "dir": "left", "ts": 1710000000 }
```

```json
{ "action": "jog_stop", "ts": 1710000000 }
```

**cmd · 设速**

```json
{ "action": "set_speed", "pan_speed": 40, "tilt_speed": 30, "ts": 1710000000 }
```

**status**

```json
{
  "pan": 135,
  "tilt": 90,
  "pan_speed": 40,
  "tilt_speed": 30,
  "step_deg": 5,
  "moving_pan": false,
  "moving_tilt": false,
  "ready": true,
  "lim_pan": [0, 270],
  "lim_tilt": [0, 180],
  "ts": 1710000000
}
```

不使用 diary 的 `tar_pitch`/`tar_roll`。以 [YAML](../protocol/deep-dog-mqtt.yml) 为准。

## Steps（前端）

- **Step 1** 校验 `capabilities.gimbal`（Hub 另需 `ext_pins.mode===pwm`）。
- **Step 2** 订阅 retain `gimbal/status`。
- **Step 3** 控制区：绝对滑块；方向 pointerdown→`jog_start` / pointerup→`jog_stop`；调速点按；`set_speed`。
- **Step 4** 绑定区：订 `handle/keymap`；`set_profile`=`gimbal`；六键 ×（press + hold）`set_keymap`。
- **Step 5** `ready===false` 禁用控制。
- **Step 6** unmount 退订。

## 固件实现

- 产品路径：`DEEP_DOG_GIMBAL_ENABLE=1`（默认）；与裸 `servo` **互斥**同 GPIO（EXT A/B）。
- 驱动层封装 pan(270°) / tilt(180°)；jog 用周期步进 + 软件限位。
- MQTT：`mqtt/modules/gimbal_mqtt`；capabilities 报 `gimbal`。
- 手柄：`profile=gimbal` 时 `HandleAppKeyMap` 执行 `gimbal.*`（见 I08a）。
- 可与 face track 联动，非本模块范围。

### 固件验收

- [ ] 绝对角到位，status 回读一致
- [ ] relative / nudge 受软件限位
- [ ] jog_start/stop 与手柄 hold 连动
- [ ] 调速点按与 set_speed 反映到 status
- [ ] 非法 JSON 不崩溃
- [ ] 引脚见 `servo_config.h` / `gimbal_config.h`

## 验收（前端）

- [ ] 详情页可调角度 / jog / 调速
- [ ] 可编辑 press/hold 绑定并 persist
- [ ] 无 capability 隐藏入口卡
