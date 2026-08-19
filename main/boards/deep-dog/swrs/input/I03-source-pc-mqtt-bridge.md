# I03 · 源：PC MQTT 桥（含 PS4）

| 项 | 内容 |
|----|------|
| source | `wifi` |
| 典型硬件 | 电脑已连接的 **PS4 DualShock 4** 或 Xbox（USB / 系统蓝牙均可） |
| 路径 | PC 读 HID → 归一化 → 发布 `handle/input` → 设备 Hub |
| 脚本 | [`scripts/deep_dog/deep_dog_handle_bridge.py`](../../../../../scripts/deep_dog/deep_dog_handle_bridge.py) |
| 实测图 | [assets/ps4-hid-map.png](./assets/ps4-hid-map.png)（Linux 标注；pygame 0-based） |

## 为何现实

- ESP32-S3 **不能**无线直连 PS4；电脑可以。
- 现有 MQTT 前缀与 Broker 已通（见 [infra](../vision/infra.md)）。
- 契约增加下行 **`handle/input`**，与 `handle/status` 快照同构，设备侧与板载 BT **同一 Hub**。

## 数据流

```text
PS4/Xbox ──(OS HID)──► Python 桥（Normalize）
                         │  publish QoS0
                         ▼
              …/handle/input  (downlink)
                         │
                         ▼
              deep-dog HandleEventHub
                         │
              handle/status ↑（合并后给网页）
```

前端 **只订** `handle/status`，不订 `handle/input`。

`--touchpad-xy` 时附带可选 `motion`（I07）：accel 重映射 `(-x,z,-y)`；gyro 用 ` (x,-z,y)` 保持右手系。平放 `accel_z≈-1`；右侧朝下 `accel_x≈+1`。对着 +轴看：**逆时针 gyro 正、顺时针负**。

## PS4 DualShock 4 · pygame / Linux HID 对照（定稿）

图注数字为 **0-based** 按钮下标；轴标签 `axes N` 若为 1-based 则 pygame 下标 = N−1。下列表统一 **pygame 0-based**。

### 按钮 → 抽象字段

| pygame `button` | 物理键 | 抽象 `buttons.*` |
|-----------------|--------|------------------|
| 0 | ✕ Cross | `a` |
| 1 | ○ Circle | `b` |
| 2 | △ Triangle | `y` |
| 3 | □ Square | `x` |
| 4 | L1 | `l1` |
| 5 | R1 | `r1` |
| 6 | L2（数字沿） | （优先用轴；作扳机轴缺失时的后备） |
| 7 | R2（数字沿） | 同上 |
| 8 | Share | `select` |
| 9 | Options | `start` |
| 10 | PS | 可选 `ps`（`ds4_linux`） |
| 11 / 12 | L3 / R3 | 可选 `l3`/`r3` |

> **常见错误**：把 `button` 下标 0..3 直接当成 `a,b,x,y`。正确是 `a,b,y,x`（△→`y`，□→`x`）。

### 轴 → 抽象字段

| pygame `axis` | 物理 | 原始极性（实测） | 抽象字段与处理 |
|---------------|------|------------------|----------------|
| 0 | 左杆水平 | 左 = +1，右 = −1 | `lx = −raw`（右为正） |
| 1 | 左杆垂直 | 上 = +1，下 = −1 | `ly = −raw`（下为正；前推 `ly<0`） |
| 2 | L2 | 按下 = +1，松开 = −1 | `l2 = (raw+1)/2` ∈ [0,1] |
| 3 | 右杆水平 | 左 = +1，右 = −1 | `rx = −raw` |
| 4 | 右杆垂直 | 上 = +1，下 = −1 | `ry = −raw` |
| 5 | R2 | 按下 = +1，松开 = −1 | `r2 = (raw+1)/2` ∈ [0,1] |
| 6 / 7 | 十字键 H/V | 同左/上为正 | v0.1 不上报（可选扩展） |

### 操作系统差异（重要）

同一 PS4，**Linux HID 标注图 ≠ macOS pygame2「PS4 Controller」表**：

| | `ds4_linux` | `ds4_sdl`（Mac 实测） |
|--|-------------|----------------------|
| 右杆 | axis 3/4 | axis 2/3 |
| L2/R2 | axis 2/5 | axis 4/5 |
| □ / △ | btn 3 / 2 | btn 2 / 3 |
| Share / L1 / Options | 8 / 4 / 9 | 4 / 9 / 6 |
| D-pad | axis 6/7 | btn 11–14 |
| 摇杆极性 | 左/上 = +1（需取反） | 右为正；pygame 垂直上=+1 → **`ly/ry = −raw`** 对齐 I01 |

**HID `--touchpad-xy`**：DS4 report Y 字节上推→负，**已是 I01**，桥端**不要**再对 `ly/ry` 取反（曾为迁就前端圆点反号而误取反）。

前端圆点：`py = ly * travel`（CSS Y 向下为正）；前推 `ly<0` → 圆点上移。勿再对 `ly` 乘 −1。

桥 `--layout auto` 按空闲轴启发式选择；Mac 请确认日志里 `layout=ds4_sdl`。

误用 `ds4_linux` 在 Mac 上的典型症状：右杆静置 `ry≈1`、L2≈0.5、Share 亮成 L1、左右摇杆对调。

### Xbox 与 HID

Mac 蓝牙下 `hidapi` 可枚举并读 **17B** 输入 report；**无陀螺仪/触控**。相对 pygame，HID 主要可能多出 Share 位 / 电池（系统或协议仍可能吞 Share）。日常桥用 pygame；细节见 [`scripts/deep_dog/README.md`](../../../../../scripts/deep_dog/README.md)。

### Xbox（pygame）— 两套轴表，勿混用

| | `xbox_sdl`（**macOS Series X 实测**） | `xbox_xinput`（Linux / 经典 XInput） |
|--|--------------------------------------|--------------------------------------|
| 静置 axes | `[0,0,0,0,-1,-1]` | `[0,0,-1,0,0,-1]` |
| 左 / 右杆 | 0/1 · **2/3** | 0/1 · **3/4** |
| LT / RT | **4 / 5** | **2 / 5** |
| View/Menu/LB… | btn 4/6/9…（同 ds4_sdl 键位骨架） | btn 6/7/4… |
| D-pad | btn 11–14（`hats=0`） | hat0 |

`--layout xbox` / `auto`：按静置轴与 `hats` **自动选** `xbox_sdl` 或 `xbox_xinput`。

误用 `xbox_xinput` 在 Mac 上的典型症状（与误用 `ds4_linux` 同源）：静置 `ry≈-1`、`l2≈0.5`；右杆左右变成 L2；右杆上下变成左右；View→L1；方向键无效。

**真源**：`--probe` 先打 `RAW IDLE axes` 与各 layout 解释，再决定映射；契约层只保留抽象字段，不把错误 XInput 表当原始数据。

## 桥职责（需求）

| 项 | 要求 |
|----|------|
| 读入 | pygame（当前） |
| 映射 | 上表；`--layout auto\|ds4\|xbox\|xbox_sdl\|xbox_xinput` |
| 发布 | `deepdiary/deep-dog/{device_id}/handle/input` |
| `device_id` | 默认 **`auto`**：订 `…/+/device/info`（+ `device/status`），优先在线且 `capabilities.handle`；恰好一台则锁定；多台用 `~/.cache/deep-dog/handle_bridge_device_id` 或要求 `--device-id`；环境变量 `DEEP_DOG_DEVICE_ID` 同显式 |
| 节流 | on_change 或 ≤20～30 Hz |
| 断线 | 桥发心跳；手柄休眠/拔出发 `connected:false`；**唤醒后自动 rescan 重连，无需重启脚本**；停发后设备超时 **1000ms** 清零（容忍短暂 MQTT 抖动） |
| 触控 XY | **默认 HID 全量读**（触控双点 + motion + 灯震）；`--no-touchpad-xy` 回退 pygame（Xbox/通用输入 + **output 震动**）；探测见 `scripts/deep_dog/deep_dog_ds4_touchpad_probe.py`；[I06](./I06-touchpad-xy.md) · [I09](./I09-ds4-output-feedback.md) |
| 凭证 | 环境变量，禁止写入仓库 |

## 样例 payload

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

## 用法

```bash
pip3 install paho-mqtt hidapi pygame
# 默认：DS4 HID + --device-id auto（扫 device/info）
python3 scripts/deep_dog/deep_dog_handle_bridge.py --via lan
# 显式 id（绑后 MAC 或未绑 dev）
python3 scripts/deep_dog/deep_dog_handle_bridge.py --via lan --device-id 1051db847e88
# Xbox：pygame 输入 + output 震动（macOS 优先蓝牙连手柄）
python3 scripts/deep_dog/deep_dog_handle_bridge.py --via lan --layout xbox
# 看 RAW 轴/键 + 各 layout 静置解释（排查映射必跑）
python3 scripts/deep_dog/deep_dog_handle_bridge.py --probe
# 启动时无手柄则等待
python3 scripts/deep_dog/deep_dog_handle_bridge.py --via lan --wait-pad
# 本地冒烟（不经 MQTT）
python3 scripts/deep_dog/deep_dog_handle_bridge.py --probe-output
python3 scripts/deep_dog/deep_dog_handle_bridge.py --probe-xbox-rumble
```

日常固件关板载 BT（见 [handle/sources/README](../../handle/sources/README.md)）；手柄与震动均走本桥。

## 验收（本源）

- [x] 文档与 YAML 含 `handle/input`
- [x] PS4 HID ↔ 抽象对照表定稿（含极性）
- [x] PC 脚本按表归一化；设备透传 `handle/status`
- [x] 默认 HID + `handle/cmd` `output` 反馈（I09）
- [x] pygame 路径 Xbox `output` 震动
- [ ] 断流 / `connected:false` 后狗控停止或轴归零（联调勾选）
