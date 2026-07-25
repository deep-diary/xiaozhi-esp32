# 03 · imu（加速度 / 陀螺 / 动作开关）

| 项 | 内容 |
|----|------|
| module_id | `imu` |
| capabilities | `imu` |
| 路由建议 | `/device/:deviceId/modules/imu` |
| 契约 | ready（六轴字段）；开关字段 planned（文档已定，固件待落地） |
| YAML | `imu/status` |
| 芯片 | BMI270（对齐 thumble） |

## 入口卡文案

- 标题：IMU  
- 说明：加速度、姿态与动作开关  

## 详情页目标

只读展示 accel / gyro / pitch / roll / accel_g，以及 **12 路**动作开关脉冲指示（`switches.*` 本周期边沿次数 > 0 时高亮/闪一下）。

## 设计说明 · 频率分层

芯片 ODR 已是 200 Hz，但若仅在 MQTT 定时器（~10 Hz）里读数，短促旋转/平移易漏检。开关识别**不得**依赖 MQTT 周期，须拆成三层：

| 层级 | 频率 | 用途 |
|------|------|------|
| 芯片 ODR | **200 Hz** | 硬件上限（BMI270 已配置） |
| 本地采集 / 开关识别 | **100 Hz**（目标） | 独立 timer/task 读 `ReadRawData`；旋转/平移边沿检测 + 去抖 |
| MQTT `imu/status` | **~10 Hz** | 六轴遥测 + 本周期开关边沿计数 |

100 Hz 相对 10 Hz 识别更稳；不必强拉满 200 Hz（I2C / CPU 更省，对人体尺度动作已够用）。

**明确不做**：位移 / 位置推算（二次积分不可靠，非本模块目标）。

## 动作开关（12 路边沿脉冲）

由陀螺与加速度导出 **12 路触发型开关**（6 旋转 + 6 平移）。语义为**边沿脉冲**，不是保持电平：识别到一次 → 本地调度调用一次，并计入本 MQTT 周期的边沿次数。

坐标均为**机体系**（芯片轴向）；桌上「左右转」对应哪根轴取决于安装朝向。

| id | 类型 | 传感器 | 触发含义（需求级；阈值留给实现） |
|----|------|--------|----------------------------------|
| `rot_x_pos` / `rot_x_neg` | 旋转 ± | 陀螺 | 绕 X 轴正/负向累计转角超过阈值（建议默认约 60°～90°） |
| `rot_y_pos` / `rot_y_neg` | 旋转 ± | 陀螺 | 绕 Y 轴正/负向同上 |
| `rot_z_pos` / `rot_z_neg` | 旋转 ± | 陀螺 | 绕 Z 轴正/负向同上（桌上左转/右转典型落在此轴） |
| `trans_x_pos` / `trans_x_neg` | 平移 ± | 加速度 | 去重力后沿 X 正/负向线性冲击超过阈值 |
| `trans_y_pos` / `trans_y_neg` | 平移 ± | 加速度 | 沿 Y 正/负向同上 |
| `trans_z_pos` / `trans_z_neg` | 平移 ± | 加速度 | 沿 Z 正/负向同上 |

与「翻面」（重力符号翻转）**不是同一类事件**；本模块不做独立 `flip_*`。

- **去抖 / 冷却**：实现须带 cooldown，避免一次动作连打。建议默认：rot 300–500 ms，trans 150–300 ms（实现参数，v0.1 **不**经 MQTT 调）。
- **主轴 / 互斥**：一次手势常多轴耦合；实现应选主轴（或「谁先过阈」）并避免同窗 rot/trans 互相误触发。
- **旋转积分**：短时手势窗口内对陀螺积分，过阈触发后清零/进入冷却；不追求长期航向。
- **`ok=false`**：`switches` 全 0，且**不**调用调度入口。
- v0.1 **无** `imu/cmd`；阈值不下发。

### 本机调度入口

- **12** 个命名入口，与上表 id 一一对应。
- **默认行为**：`ESP_LOGI` 打印触发 id（及可选强度 / 转角）。
- **扩展点**：后续可挂系统函数（表情、狗控、LED 等）。
- 调度在**本地识别边沿时立即触发**，不依赖 MQTT 10 Hz。
- 回调须短；重活投递队列（对齐 touch → DogControl），禁止在识别 ISR / 高频路径里直接做重活。

## Topic（前端联调）

| Topic | 方向 | QoS | retain | 频率 |
|-------|------|-----|--------|------|
| `deepdiary/deep-dog/{deviceId}/imu/status` | ↑ | 0 | **false** | ~10 Hz |

默认 `deviceId=dev` → 全路径：

`deepdiary/deep-dog/dev/imu/status`

另需读 `device/info` 的 `capabilities.imu`（应为 `true` 才显示入口卡）。

**无下行** `imu/cmd`（v0.1）。不新增开关专用 topic；边沿计数并入 `imu/status`。

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
  "switches": {
    "rot_x_pos": 0,
    "rot_x_neg": 0,
    "rot_y_pos": 0,
    "rot_y_neg": 0,
    "rot_z_pos": 1,
    "rot_z_neg": 0,
    "trans_x_pos": 0,
    "trans_x_neg": 0,
    "trans_y_pos": 0,
    "trans_y_neg": 1,
    "trans_z_pos": 0,
    "trans_z_neg": 0
  },
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
| `switches.rot_*_pos/neg` | int | 自上一帧以来该轴正/负向旋转边沿次数（≥ 0） |
| `switches.trans_*_pos/neg` | int | 自上一帧以来该轴正/负向平移边沿次数（≥ 0） |
| `ts` | int | unix 秒 |

字段以 [YAML](../protocol/deep-dog-mqtt.yml) 为准。

## Steps（前端）

- **Step 1** 校验 `capabilities.imu`。
- **Step 2** 订阅 `imu/status`（非 retain，需等下一帧）。
- **Step 3** 渲染六轴与姿态（可图表）；`ok===false` 显示未就绪。
- **Step 4** 渲染 12 路开关：本帧 `switches.* > 0` 时脉冲高亮/闪一下（可叠加次数角标；建议分组：旋转 6 + 平移 6）。
- **Step 5** 无下行 cmd（v0.1）。
- **Step 6** unmount 退订。

## 固件实现

### 已落地

- BMI270：[`sensor/imu_sensor`](../../../sensor/imu_sensor.h)（I2C 与 codec 共用 `i2c_bus`）；芯片 ODR 200 Hz。
- MQTT：[`mqtt/modules/imu_mqtt`](../../../mqtt/modules/imu_mqtt.h)，约 10 Hz 发六轴 + pitch/roll；无芯片时仍发 `ok=false`。
- `capabilities.imu=true`（`DEEP_DOG_IMU_ENABLE=1`）。

### 待落地（开关）

- 独立 **100 Hz** 本地采集任务（勿仅跟 MQTT 10 Hz 读数）。
- 陀螺短时积分 → `rot_*_pos/neg`；去重力冲击 → `trans_*_pos/neg`；cooldown + 主轴/互斥。
- **12** 路调度入口（默认 log）。
- `imu/status` 附带本周期 `switches.*` 边沿计数后清零/累加窗口重置。

## 验证脚本

```bash
/usr/bin/python3 scripts/deep_dog_mqtt_imu_verify.py --via web --wait 8
/usr/bin/python3 scripts/deep_dog_mqtt_imu_verify.py --via lan --wait 8
/usr/bin/python3 scripts/deep_dog_mqtt_imu_verify.py --via both --min-msgs 5
```

（开关字段落地后，脚本可增检 `switches` 键存在；动作触发验收以手动绕轴旋转约 90° / 沿轴甩动为主。）

## 验收

- [x] 固件发布 `imu/status`（有/无芯片均可）
- [x] `device/info` 含 `capabilities.imu=true`
- [ ] 详情页可见三轴与姿态（前端）
- [ ] 详情页可见 12 路开关脉冲（前端；固件 `switches` 落地后）
- [ ] 无 capability 隐藏入口卡（前端）
- [ ] 本地 100 Hz 采集 + 边沿识别（固件）
- [ ] 12 路调度入口默认仅打 log（固件）
- [ ] `imu/status` 含 12 路 `switches.*` 边沿计数（固件）
