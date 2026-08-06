# 14 · motor（单电机控制）

| 项 | 内容 |
|----|------|
| module_id | `motor` |
| capabilities | `motor` |
| 路由建议 | `/device/:deviceId/modules/motor` |
| 契约 | ready（本轮） |
| YAML | `motor/cmd`、`motor/status` |
| 需求 | [swrs/motor/03](../../motor/03-motor-mqtt.md) |
| 驱动 | [`motor/`](../../../motor/) |
| 显示条件 | `capabilities.motor === true`（常与 `ext_pins.mode=can` 同时） |

## 入口卡文案

- 标题：电机  
- 说明：单电机使能 / 位置 / 反馈  

## 详情页目标

选 `motor_id`，使能/失能，写位置（rad）与限速；列表回读实际角/速/矩。  
手柄绑定编辑走 [11-handle](./11-handle.md)（I08a/I08b），本页不重复轴编辑器。  
原始帧监视走 [12-can](./12-can.md)。

## Topic

| Topic | 方向 | QoS | retain |
|-------|------|-----|--------|
| `motor/status` | ↑ | 0 | true |
| `motor/tools` | ↑ | 0 | true |
| `motor/mcp_result` | ↑ | 0 | false |
| `motor/cmd` | ↓ | 1 | false |

前缀：`deepdiary/deep-dog/{device_id}/`。

## 字段表

### `motor/cmd`（稀疏）

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `motor_id` | int | 是 | 1～255 |
| `enable` | bool | 否 | `true`→initialize/enable；`false`→reset |
| `reset` | bool | 否 | `true`→resetMotor |
| `position_rad` | float | 否 | 位置参考，钳位 ±12.57 |
| `speed_limit` | float | 否 | 限速 rad/s |
| `iq_ref` | float | 否 | 电流环目标 |
| `mit` | object | 否 | `{ position_rad, velocity_rad_s, kp, kd, torque_ff }` |
| `mcp_call` | object | 否 | `{ name, arguments }` 执行 `self.motor.*` / `self.can.*` 工具（MOT-10） |
| `ts` | int | 否 | Unix 秒 |

### `motor/status`

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | bool | 栈就绪 |
| `motors` | array | 已注册电机快照 |
| `motors[].id` | int | motor_id |
| `motors[].init_state` | string | `none` / `initializing` / `ready` / `failed`（MOT-09） |
| `motors[].position_rad` | float | 实际角 |
| `motors[].speed_rad_s` | float | 实际速 |
| `motors[].torque_nm` | float | 实际矩 |
| `motors[].temperature` | float | 可选 |
| `motors[].fault` | bool | 可选 |
| `motors[].target_rad` | float | 可选软件目标 |
| `motors[].max_abs_torque` | float | 历史最大 \|扭矩\|（N·m） |
| `motors[].has_device_id` | bool | 是否已收到通信类型 0 应答 |
| `motors[].mcu_uid_hex` | string | 16 位十六进制 MCU UID |
| `motors[].version` | string | EL05 软件版本（扫描后自动查询） |
| `motors[].has_feedback` | bool | 是否收到过反馈帧 |
| `motors[].feedback_seq` | int | 反馈帧计数 |
| `motors[].mode_status` | string | `reset` / `calibrate` / `run` |
| `motors[].master_id` | int | 主站 CAN ID |
| `motors[].collision` | bool | 堵转/碰撞 latch |
| `ts` | int | Unix 秒 |

以 [YAML](../protocol/deep-dog-mqtt.yml) 为准。

## 样例 JSON

```json
{ "motor_id": 1, "enable": true, "position_rad": 1.57, "speed_limit": 5.0, "ts": 1710000000 }
```

```json
{
  "ok": true,
  "motors": [
    {
      "id": 1,
      "position_rad": 0.12,
      "speed_rad_s": 0.0,
      "torque_nm": 0.05,
      "target_rad": 1.57,
      "fault": false
    }
  ],
  "ts": 1710000000
}
```

## Steps（前端）

- **Step 1** 校验 `capabilities.motor`。
- **Step 2** 订 `motor/status`（retain）。
- **Step 3** 发 `motor/cmd` 使能与点动；强提示力矩风险。
- **Step 4** 手柄映射链到 handle 模块或嵌入 `HandleKeymapEditor`（`profile=motor`）。
- **Step 5** unmount 退订。

## 固件实现

- `mqtt/modules/motor_mqtt.*`；`#if DEEP_DOG_MOTOR_ENABLE`
- 对接 `DeepMotor` / `MotorProtocol`

## 验收

- [ ] 无 capability 隐藏入口卡
- [ ] 使能后 status 有反馈（或超时仍可发位置）
- [ ] 与 can 透传页并存不冲突
