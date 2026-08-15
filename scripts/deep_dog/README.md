# deep-dog 自研脚本

板级联调 / 验收工具集（与上游 `scripts/p3_tools`、`Image_Converter` 等分开）。

## MQTT / 视觉 / 审计

| 脚本 | 用途 |
|------|------|
| `deep_dog_mqtt_verify.py` | device/stream MQTT 验收 |
| `deep_dog_mqtt_face_verify.py` | face/cmd + face/status |
| `deep_dog_device_audit.py` | 人脸库 + mem + tasks（MQTT/WS MCP） |

```bash
python3 scripts/deep_dog/deep_dog_device_audit.py --device-ip 192.168.31.211 --via web --wait 8
```

## 手柄 / 输入

| 脚本 | 用途 |
|------|------|
| `deep_dog_handle_bridge.py` | PC → MQTT `handle/input`；DS4=HID 全量，Xbox=pygame + `output` 震 |
| `deep_dog_ds4_hid.py` | DS4 HID 解析 / 灯震（桥与 probe 共用） |
| `deep_dog_ds4_touchpad_probe.py` | DS4 触控板 XY 冒烟 |
| `deep_dog_fetch_bluepad32.sh` | 拉取 Bluepad32 + BTstack（瘦剖面再开板载 BT） |

```bash
# DS4（灯+震+触控+motion）
python3 scripts/deep_dog/deep_dog_handle_bridge.py --via lan

# Xbox（pygame；macOS 优先蓝牙）
python3 scripts/deep_dog/deep_dog_handle_bridge.py --via lan --layout xbox

# 真源对照（RAW 轴/键）
python3 scripts/deep_dog/deep_dog_handle_bridge.py --probe
```

### Xbox：HID 还能读到什么？（对照 pygame）

本机 Mac + **蓝牙** Series 实测：`hidapi` 能枚举 `Xbox Wireless Controller`（VID `0x045E` PID `0x0B13`），并读到 **17 字节**输入 report（report id `0x01`）。

| 能力 | pygame / GC | 原始 HID（hidapi） | 备注 |
|------|-------------|-------------------|------|
| 双摇杆 / 扳机 / 面键 / 肩键 / View·Menu·Guide / L3·R3 / D-pad | ✅ | ✅（需按 GIP/Xbox 报告解析） | 桥已用 pygame 覆盖日常 |
| **Share（截图键）** | ❌（本机未进 pygame） | ⚠ 报告里常有位，Mac 上可能被系统截走 | 未进契约；要接需单独解析 |
| **震动 output** | ✅ `Joystick.rumble`（蓝牙） | ✅ 厂商 output report（SDL 同路径） | USB 有线 Mac 常不震 |
| 电池电量 | 少见 | ⚠ 另有 report / 特征，平台相关 | 未做 |
| 触控板 | ❌（无硬件） | ❌ | |
| **陀螺仪 / 加速度** | ❌ | ❌ | Xbox 手柄无 IMU；`motion` 仅 DS4 |
| 灯条 RGB | ❌ | ❌ | 仅 DS4 |

结论：Xbox 走 HID **多不出陀螺仪/触控**；主要增益是 **Share 位（若系统放行）**、电池、以及不依赖 pygame 的解析。日常桥继续 pygame 即可；DS4 才值得 HID 全量。

规格：[I03](../../main/boards/deep-dog/swrs/input/I03-source-pc-mqtt-bridge.md) · [I07](../../main/boards/deep-dog/swrs/input/I07-motion-gyro.md) · [I09](../../main/boards/deep-dog/swrs/input/I09-ds4-output-feedback.md)

## MQTT / 视觉验收

| 脚本 | 用途 |
|------|------|
| `deep_dog_mqtt_verify.py` | 板级 MQTT / 推流冒烟 |
| `deep_dog_mqtt_face_verify.py` | face 模块 |
| `deep_dog_mqtt_track_verify.py` | track 模块 |
| `deep_dog_mqtt_imu_verify.py` | imu 模块 |
| `deep_dog_rtsp_pull_verify.py` | RTSP 拉流落盘 |
| `deep_dog_hls_embed.html` | HLS 嵌入页（辅助） |

```bash
python3 scripts/deep_dog/deep_dog_mqtt_verify.py --via lan --wait 20
python3 scripts/deep_dog/deep_dog_rtsp_pull_verify.py --outdir h264 --python-only
```
