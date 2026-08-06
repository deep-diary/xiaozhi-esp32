# ws-mcp · 局域网 WebSocket → MCP 通用桥接

与 [`http-server/`](../http-server/) **独立 httpd**；设备内 Web UI 逐步弃用，本地/局域网调试经本模块直连 **全部已注册 MCP 工具**（motor / dog / led / servo …）。

## 开关

| 宏 | 默认 | 说明 |
|----|------|------|
| `DEEP_DOG_WS_MCP_ENABLE` | `1` | 置 `0` 关闭（桩类，不占 httpd） |
| `DEEP_DOG_WS_MCP_PORT` | `8080` | 监听端口 |
| `DEEP_DOG_WS_MCP_PATH` | `/ws` | WebSocket 路径 |

依赖 Kconfig：`CONFIG_HTTPD_WS_SUPPORT=y`（已写入 `config.json`）。

与 `DEEP_DOG_HTTP_SERVER_ENABLE` **不可同端口**（编译期 `#error`）。

## 协议

连接：`ws://{device-ip}:{port}{path}`

**入站**（二选一）：

1. MCP JSON-RPC 直发（推荐，与云端一致）  
2. 信封：`{"type":"mcp","payload":{ ... JSON-RPC ... }}`

示例 `tools/call`：

```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": { "name": "self.motor.scan_bus", "arguments": {} },
  "id": 1
}
```

**出站**：`Application::RegisterMcpBroadcastCallback` 将 MCP 响应 **广播** 到所有已连接 WS 客户端。

## 需求文档

[swrs/ws-mcp/01-local-mcp-bridge.md](../swrs/ws-mcp/01-local-mcp-bridge.md)

## 与 MQTT

并存：远程走 EMQX `motor/*` 等；局域网全量 MCP 走 WS，无需为每个能力补 MQTT topic。
