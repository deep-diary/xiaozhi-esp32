# I03 · 源：PC MQTT 桥（含 PS4）

| 项 | 内容 |
|----|------|
| source | `wifi` |
| 典型硬件 | 电脑已连接的 **PS4 DualShock 4** 或 Xbox（USB / 系统蓝牙均可） |
| 路径 | PC 读 HID → 归一化 → 发布 `handle/input` → 设备 Hub |
| 本轮 | **仅需求**；脚本可后置 `scripts/` 或 `tools/` |

## 为何现实

- ESP32-S3 **不能**无线直连 PS4；电脑可以。
- 现有 MQTT 前缀与 Broker 已通（见 [infra](../vision/infra.md)）。
- 契约增加下行 **`handle/input`**，与 `handle/status` 快照同构，设备侧与板载 BT **同一 Hub**。

## 数据流

```text
PS4/Xbox ──(OS HID)──► Python 桥
                         │  publish QoS0/1
                         ▼
              …/handle/input  (downlink)
                         │
                         ▼
              deep-dog HandleEventHub
                         │
              handle/status ↑（合并后给网页）
```

## 桥职责（需求）

| 项 | 要求 |
|----|------|
| 读入 | pygame / hidapi / inputs 等读轴与键 |
| 映射 | 索尼键位 → 抽象 `a/b/x/y/...`（实现时钉死对照表） |
| 发布 | Topic：`deepdiary/deep-dog/{device_id}/handle/input` |
| 节流 | 建议 on_change 或 ≤20～30 Hz；避免打满 Broker |
| 断线 | 手柄断开发 `connected:false` 一帧；或停发并由设备超时清零（实现选一，默认：**超时 500ms 无包则视为断开并清零轴**） |
| 凭证 | Broker 用户密码走环境变量，禁止写入仓库 |

## 样例 payload

与 [11-handle](../mqtt/modules/11-handle.md) / YAML 一致：

```json
{
  "connected": true,
  "source": "wifi",
  "axes": { "lx": 0.1, "ly": -0.2, "rx": 0.0, "ry": 0.0 },
  "buttons": {
    "a": false, "b": false, "x": false, "y": false,
    "l1": false, "r1": false, "l2": 0.0, "r2": 0.0,
    "start": false, "select": false
  },
  "ts": 1710000000
}
```

## 与板载 BT 并列

- 两源均为 v0.1 一等公民；**后到覆盖**（见 I01）。
- PC 桥可在 **未扩 Flash / 未接 Bluepad32** 时先联调狗控与网页 status。
- 网页也可发 `handle/input` 做虚拟摇杆调试（同 Topic）。

## 用法（脚本）

```bash
pip3 install paho-mqtt pygame
# 局域网（默认）
/usr/bin/python3 scripts/deep_dog_handle_bridge.py --via lan --device-id dev
# 列出手柄
/usr/bin/python3 scripts/deep_dog_handle_bridge.py --list-joysticks
# 外网 WSS
export DEEP_DOG_MQTT_USER=... DEEP_DOG_MQTT_PASS=...
/usr/bin/python3 scripts/deep_dog_handle_bridge.py --via web
```

脚本路径：仓库根目录 [`scripts/deep_dog_handle_bridge.py`](../../../../../scripts/deep_dog_handle_bridge.py)。

## 验收（本源）

- [x] 文档与 YAML 含 `handle/input`
- [ ] （实现）PC 脚本推轴，设备 `handle/status` 同步且 `source=wifi`
- [ ] （实现）断流 / `connected:false` 后狗控停止或轴归零
