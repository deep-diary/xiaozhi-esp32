# I07 · DS4 手柄 motion（陀螺仪 / 加速度）

| 项 | 内容 |
|----|------|
| 状态 | **契约 + 桥 + 固件透传**；前端展示 P1；云台 App planned |
| 范围 | PS4 DualShock 全量 HID 中的 **gyro / accel**；经 `handle` 可选字段上报 |
| 前置 | [I03](./I03-source-pc-mqtt-bridge.md) · [I06](./I06-touchpad-xy.md)（同 `--touchpad-xy` HID 路径） |
| 非目标 | 与板载 BMI270 `imu/status` 合并；触控手势枚举（→ I08） |

## 「可选 motion」含义

`motion` = 手柄内 IMU 瞬时读数，与摇杆/按键同级挂在快照上。

| | 说明 |
|--|------|
| 有 | DS4 + `--touchpad-xy`（hidapi 全量 report）时带上 |
| 无 | pygame / Xbox / 未开 HID → **整段省略**；旧端忽略 |
| 单位 | `gyro_*`：**dps**；`accel_*`：**g**（约 ±4g 满量程） |
| ≠ | `imu/status`（ESP32 板载 BMI270） |

## 契约

`handle/input` 与 `handle/status` 同构，可选：

```json
"motion": {
  "gyro_x": 12.5,
  "gyro_y": -3.0,
  "gyro_z": 0.2,
  "accel_x": 0.02,
  "accel_y": 0.21,
  "accel_z": -0.98
}
```

上例为 **平放桌面、触摸面朝上** 的典型静置（`accel_z ≈ -1 g`；`accel_y` 可有小残余）。

## 坐标系（钉死，前端勿再猜）

手柄 **机体系**（右手系），由 PC 桥从 DS4 HID raw 重映射后上报：

| 轴 | 正方向 | 静置读数（重力约定：轴朝下 ≈ **+1 g**） |
|----|--------|------------------------------------------|
| `+X` | 手柄右侧（R1 一侧） | 平放 ≈0；**右侧朝下 → `accel_x ≈ +1 g`** |
| `+Y` | 手柄前方（朝光条 / 远离玩家握持端） | 平放可有小偏置；前侧朝下 → `accel_y ≈ +1 g` |
| `+Z` | 手柄上方（触摸面朝外 / 向上） | **平放面朝上 → `accel_z ≈ -1 g`**（+Z 朝上） |

HID raw → 机体系：

```text
accel: (x, y, z)_body = (-x,  z, -y)_hid          # R, det(R)=-1
gyro:  (x, y, z)_body = ( x, -z,  y)_hid          # det(R)·R·ω，保持右手系
```

### 旋转正方向（右手螺旋，钉死）

**对着该轴的正端看过来**（+轴指向你）：

| 旋转 | `gyro_*` |
|------|----------|
| **逆时针** | **正** |
| **顺时针** | **负** |

等价：拇指指向 +轴，四指弯曲方向 = 该轴 `gyro` 为正。

手测（平放握持；对着 +轴正端看：逆时针为正、顺时针为负）：

| 动作 | 期望 |
|------|------|
| 光条端抬起（绕 +X） | `gyro_x > 0` |
| 右侧往下压（绕 +Y） | `gyro_y < 0`（顶面向右倾 → 正；右侧下压为负） |
| 水平面内光条往右偏（绕 +Z） | `gyro_z < 0`（俯视顺时针为负） |

说明：

- Sony **没有**对外公布与 MQTT 字段一一对应的「官方手柄轴」文档；社区/仿真器常见约定不一。
- deep-dog **以本表为准**；加速度「轴朝下 ≈ +1 g」、平放 `accel_z ≈ -1`。
- 摇杆契约（I01：右/下为正）与本机体系独立；「前」与 `+Y` 同向：前推左杆 → `ly < 0`。

## App 映射（占位）

| App | 建议 | 本轮 |
|-----|------|------|
| 前端 | 三轴条 / 数值 | 需求同步 |
| `HandleAppGimbal` | gyro 增量 → pan/tilt | **planned**，不在本轮实现闭环 |
| 不倒翁 | 姿态辅助 | 更后 |

业务不进 MQTT 层；见 [I04](./I04-apps-mapping.md)。

## 手势

触控 **滑动/捏合** 等为软件识别，**本需求不做**（I08）。

## 验收

- [x] YAML 含可选 `motion`
- [x] 桥在 `--touchpad-xy` 下上报 motion
- [x] 固件 `HandleMotion` + `handle/status` 透传
- [x] 前端需求 REQ-IOT-230 含 MOTION 区
- [x] 设备 status 透传
- [ ] 前端 MOTION 区可见（P1）
- [ ] HandleAppGimbal（后续）
