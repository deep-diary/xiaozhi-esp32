# 09 · gimbal（云台）

| 项 | 内容 |
|----|------|
| module_id | `gimbal` |
| capabilities | `gimbal` |
| 路由建议 | `/device/:deviceId/modules/gimbal` |
| 契约 | ready（字段）；实现更后 |
| YAML | `gimbal/cmd`、`gimbal/status` |
| 路线图 | **V-C04**（原 M03） |
| 参考 | [deep-diary/gimbal](../../../../deep-diary/gimbal/)、[servo](./10-servo.md) |

## 入口卡文案

- 标题：云台  
- 说明：云台 pan / tilt  

## 详情页目标

绝对/相对调节 pan、tilt；回读角度与限位。

## Topic

| Topic | 方向 | QoS | retain |
|-------|------|-----|--------|
| `gimbal/status` | ↑ | 0 | true |
| `gimbal/cmd` | ↓ | 1 | false |

## 样例 JSON

**cmd**

```json
{ "mode": "absolute", "pan": 135, "tilt": 90, "speed": 0, "ts": 1710000000 }
```

**status**

```json
{
  "pan": 135,
  "tilt": 90,
  "ready": true,
  "lim_pan": [0, 270],
  "lim_tilt": [0, 180],
  "ts": 1710000000
}
```

不使用 diary 的 `tar_pitch`/`tar_roll`。以 [YAML](../protocol/deep-dog-mqtt.yml) 为准。

## Steps（前端）

- **Step 1** 校验 `capabilities.gimbal`。
- **Step 2** 订阅 `gimbal/status`。
- **Step 3** 滑块/输入 absolute；方向键 relative。
- **Step 4** 发 `gimbal/cmd`；`ready===false` 禁用控制。
- **Step 5** unmount 退订。

## 固件实现

- 依赖舵机驱动；产品路径用 `gimbal/*`，裸调试见 [10-servo](./10-servo.md)。
- 依赖 MQTT 客户端骨架（与 [02-stream](./02-stream.md) 同栈）。
- 引脚/供电须实机确认（不可照搬 diary 与 CAN 冲突脚）。
- 可与 face track 联动，非本模块范围。

### 固件验收

- [ ] 绝对角到位，status 回读一致
- [ ] relative 受软件限位
- [ ] 非法 JSON 不崩溃
- [ ] 引脚写入 `config.h`（实现时）

## 验收（前端）

- [ ] 详情页可调角度
- [ ] 无 capability 隐藏入口卡
