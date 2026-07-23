# 03 · imu（加速度 / 陀螺）

| 项 | 内容 |
|----|------|
| module_id | `imu` |
| capabilities | `imu` |
| 路由建议 | `/device/:deviceId/modules/imu` |
| 契约 | ready（字段）；驱动 D9 |
| YAML | `imu/status` |
| 芯片 | BMI270（对齐 thumble） |

## 入口卡文案

- 标题：IMU  
- 说明：加速度与姿态  

## 详情页目标

只读展示 accel / gyro / pitch / roll / accel_g。

## Topic（前端联调）

| Topic | 方向 | QoS | retain | 频率 |
|-------|------|-----|--------|------|
| `deepdiary/deep-dog/{deviceId}/imu/status` | ↑ | 0 | **false** | ~10 Hz |

默认 `deviceId=dev` → 全路径：

`deepdiary/deep-dog/dev/imu/status`

另需读 `device/info` 的 `capabilities.imu`（应为 `true` 才显示入口卡）。

**无下行** `imu/cmd`（v0.1）。

## 样例 JSON

```json
{
  "ok": true,
  "accel_x": 0.1,
  "accel_y": -0.05,
  "accel_z": 9.8,
  "accel_g": 9.82,
  "gyro_x": 0.0,
  "gyro_y": 0.0,
  "gyro_z": 0.0,
  "pitch": 2.5,
  "roll": -1.2,
  "ts": 1710000000
}
```

| 字段 | 类型 | 单位 / 说明 |
|------|------|-------------|
| `ok` | bool | 传感器可读；`false` 时数值多为 0，详情页显示「未就绪」 |
| `accel_x/y/z` | float | m/s² |
| `accel_g` | float | √(x²+y²+z²) |
| `gyro_x/y/z` | float | dps |
| `pitch` / `roll` | float | deg（由加速度估计） |
| `ts` | int | unix 秒 |

字段以 [YAML](../protocol/deep-dog-mqtt.yml) 为准。

## Steps（前端）

- **Step 1** 校验 `capabilities.imu`。
- **Step 2** 订阅 `imu/status`（非 retain，需等下一帧）。
- **Step 3** 渲染数值（可图表）；`ok===false` 显示未就绪。
- **Step 4** 无下行 cmd（v0.1）。
- **Step 5** unmount 退订。

## 固件实现

- BMI270：[`sensor/imu_sensor`](../../../sensor/imu_sensor.h)（I2C 与 codec 共用 `i2c_bus`）。
- MQTT：[`mqtt/modules/imu_mqtt`](../../../mqtt/modules/imu_mqtt.h)，约 10 Hz；无芯片时仍发 `ok=false`。
- `capabilities.imu=true`（`DEEP_DOG_IMU_ENABLE=1`）。

## 验证脚本

```bash
/usr/bin/python3 scripts/deep_dog_mqtt_imu_verify.py --via web --wait 8
/usr/bin/python3 scripts/deep_dog_mqtt_imu_verify.py --via lan --wait 8
/usr/bin/python3 scripts/deep_dog_mqtt_imu_verify.py --via both --min-msgs 5
```

## 验收

- [x] 固件发布 `imu/status`（有/无芯片均可）
- [x] `device/info` 含 `capabilities.imu=true`
- [ ] 详情页可见三轴与姿态（前端）
- [ ] 无 capability 隐藏入口卡（前端）
