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

## Topic

| Topic | 方向 | QoS | retain |
|-------|------|-----|--------|
| `imu/status` | ↑ | 0 | false |

建议设备侧约 5–10 Hz；前端可节流刷新 UI。

## 样例 JSON

```json
{
  "ok": true,
  "accel_x": 0.1, "accel_y": -0.05, "accel_z": 9.8, "accel_g": 9.82,
  "gyro_x": 0.0, "gyro_y": 0.0, "gyro_z": 0.0,
  "pitch": 2.5, "roll": -1.2,
  "ts": 1710000000
}
```

单位：accel `m/s²`，gyro `dps`，角 `deg`。以 [YAML](../protocol/deep-dog-mqtt.yml) 为准。

## Steps（前端）

- **Step 1** 校验 `capabilities.imu`。
- **Step 2** 订阅 `imu/status`。
- **Step 3** 渲染数值（可图表）；`ok===false` 显示未就绪。
- **Step 4** 无下行 cmd（v0.1）。
- **Step 5** unmount 退订。

## 固件实现

- 依赖 [D9](../../dog/DEVELOPMENT_PLAN.md) / thumble `ImuSensor`。
- 采样可更高，MQTT 节流发布。
- 状态：planned until D9。

## 验收

- [ ] 详情页可见三轴与姿态
- [ ] 无 capability 隐藏入口卡
