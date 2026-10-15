# MOT-10 · 电机页 MCP 工具桥（MQTT）

| 项 | 内容 |
|----|------|
| ID | MOT-10 |
| 状态 | 本轮落地 |
| 依赖 | [MOT-03](./03-motor-mqtt.md) · [ws-mcp/01](../ws-mcp/01-local-mcp-bridge.md) |
| 固件 | `motor_mqtt` · `deep_motor_control` · `McpServer` |

## 目标

电机调试页可列出并执行 `self.motor.*` / `self.can.*` MCP 工具（含参数），无需语音通道。

## Topic

| Topic | 方向 | retain | 说明 |
|-------|------|--------|------|
| `motor/tools` | ↑ | true | 工具 catalog（name、description、inputSchema） |
| `motor/mcp_result` | ↑ | false | 单次 `mcp_call` 执行结果 |
| `motor/cmd.mcp_call` | ↓ | — | `{ "name": "self.motor.get_status", "arguments": { "motor_id": 1 } }` |

同网段亦可走 WebSocket MCP（`device/info.ws_mcp_port`），与本 Topic **并存**。

## 固件

- 取消 `deep_motor_control.cc` 中电机类 MCP 注释注册
- 单电机剖面默认 `DEEP_DOG_WS_MCP_ENABLE=1`

## 验收

- [ ] 连 broker 后 retain 收到 `motor/tools`，含 `self.motor.initialize` 等
- [ ] `motor/cmd` 发 `mcp_call` 后收到 `motor/mcp_result`
- [ ] 与 `motor/status` 位置点动不冲突
