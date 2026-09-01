# MOT-15 · 设置电机 CAN ID

| 项 | 内容 |
|----|------|
| ID | MOT-15 |
| 状态 | 本轮落地 |
| 依赖 | [MOT-01](./01-can-hw-profile.md) · [MOT-06](./06-bus-scan-get-device-id.md) · [MOT-14](./14-motor-mcp-tools.md) |
| 固件 | [`motor/protocol_motor`](../../motor/protocol_motor.cpp) · [`motor/deep_motor_control`](../../motor/deep_motor_control.cc) |

## 目标

多电机总线上存在 CAN ID 冲突时，可将指定电机（按当前 CAN ID 寻址）的 CAN ID 改为新值，避免拆机重烧。EL05 说明书「通信类型 7：设置电机 CAN_ID，更改当前电机 CAN_ID，立即生效」。

## CAN 协议（EL05 通信类型 7）

### 请求帧（主机 → 电机）

| 字段 | 值 |
|------|-----|
| 29-bit ID bit28–24 | `0x07`（通信类型 7） |
| 29-bit ID bit23–16 | 预设置 CAN ID（新 CAN ID，1～127） |
| 29-bit ID bit15–8 | 主站 CAN ID（`MOTOR_MASTER_ID`，默认 `0xFD`） |
| 29-bit ID bit7–0 | 目标电机当前 CAN ID（1～127） |
| 8 字节 data | **全 0** |
| dlc | 8 |

### 示例

当前 ID=0x01，改为 0x02 → 扩展 ID = `0x0702FD01`，data 全 0。

### 应答帧（电机 → 主机）

按说明书为通信类型 0 广播帧（`bit7–0 = 0xFE`，`bit23–8 = 新 CAN ID`）。本实现只发不等，应答由既有 `CanRxTask` 统一收帧路径处理（与 MOT-06 一致），不额外轮询。

## 固件架构

```text
self.motor.set_can_id(motor_id, new_id)
        │
        ▼
resolve_motor_id(motor_id) → 当前 CAN ID
        │
        ▼
MotorProtocol::setCanId(current_id, new_id) → CAN TX（只发）
        │
        └── 改后旧 ID 失效，需重新 scan_bus 发现
```

- 校验 `current_id` / `new_id` 均在 1～127（与 `sendGetDeviceIdProbe` 一致）。
- 改 ID 为破坏性操作（旧 ID 立即失效），不自动重注册槽位；由用户随后调用 `self.motor.scan_bus` 重新发现。

## MCP 工具

| 工具 | 参数 | 说明 |
|------|------|------|
| `self.motor.set_can_id` | `motor_id`（当前 ID，0=活跃电机）、`new_id`（1～127） | 设置目标电机 CAN ID，立即生效 |

归入 `14-motor-mcp-tools.md` 第 2 节「① 总线/注册/发现」组。前端经 `motor/tools` 动态 catalog 自动展示，无需前端改动。

## 验收

- [ ] 编译通过，无残留引用错误
- [ ] 调 `set_can_id{motor_id:1, new_id:2}` 抓 CAN TX 确认 `id=0x0702FD01`、data 全 0、dlc=8、extd=1
- [ ] `motor/tools` catalog 含 `self.motor.set_can_id`
- [ ] `motor_id` 越界（0 或 >127）或 `new_id` 越界时返回错误字符串，不发送 CAN 帧
- [ ] 改 ID 后重新 `scan_bus` 可在新 ID 上发现该电机

## 非目标

- 不做改 ID 的应答确认/重试（只发不等，与 MOT-06 一致）
- 不自动更新电机槽位映射或活跃电机（由 scan_bus 重新发现）
