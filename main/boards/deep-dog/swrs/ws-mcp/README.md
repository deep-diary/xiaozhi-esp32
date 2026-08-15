# ws-mcp · WebSocket MCP 桥接

| 文档 | 内容 |
|------|------|
| [01-local-mcp-bridge](./01-local-mcp-bridge.md) | 通用 WS→MCP PoC（局域网调试） |
| [../mqtt/M02-mcp-call-control-plane.md](../mqtt/M02-mcp-call-control-plane.md) | MQTT `mcp_call` 与 MCP 工具统一控制面 |

控制面原则：**指令**走 MCP（WS 或 MQTT `mcp_call`）；**状态**仍走 MQTT retain/heartbeat。详见 M02。

代码：[`ws-mcp/`](../../ws-mcp/)

Motor 场景亦见 [motor/08-local-websocket-mcp-bridge.md](../motor/08-local-websocket-mcp-bridge.md)（已并入本目录）。
