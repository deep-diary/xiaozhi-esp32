# WS-01 · 局域网 WebSocket → MCP 通用桥接（PoC）

| 项 | 内容 |
|----|------|
| ID | WS-01 |
| 状态 | **PoC 已落地** |
| 代码 | [`ws-mcp/`](../../ws-mcp/) · `DeepDogWsMcpServer` |
| 参考 | otto [`websocket_control_server.*`](../../../otto-robot/websocket_control_server.cc) |

## 目标

**通用** MCP 调试通道：前端 / 脚本按 MCP JSON-RPC 发指令，触发设备上**任意已注册工具**（motor、dog、led、servo…），不为每个能力单独定义 MQTT topic。

| 场景 | 通道 |
|------|------|
| 局域网本地调试 | `ws://{ip}:{port}/ws` → `McpServer::ParseMessage` |
| 远程 / 云控 | MQTT（`device/info` 可含 `ws_mcp_port` 供同网段发现） |
| 语音 / 云端 MCP | 与 WS 共用同一套 `AddTool` 定义 |

## 架构

- **独立 httpd**（与 `http-server/` 解耦，便于后续弃用设备内 Web UI）
- 开关：`DEEP_DOG_WS_MCP_ENABLE`（单电机剖面默认 **1**）
- 默认：`8080` + `/ws`（`DEEP_DOG_HTTP_SERVER_ENABLE=0` 时不冲突）
- MCP 响应：`RegisterMcpBroadcastCallback` → `BroadcastMessage` 到所有 WS 客户端

## 入站协议

1. **直发 JSON-RPC**（与 [`docs/mcp-usage_zh.md`](../../../../docs/mcp-usage_zh.md) 一致）
2. **信封**：`{"type":"mcp","payload":{...}}`

支持 `tools/list`、`tools/call`、`initialize` 等标准 MCP 方法。

## device/info 扩展（可选字段）

| 字段 | 说明 |
|------|------|
| `ws_mcp_port` | WS 监听端口（未启用则不出现） |
| `ws_mcp_path` | 默认 `/ws` |
| `capabilities.ws_mcp` | `true` |

## 验收

- [x] `DEEP_DOG_WS_MCP_ENABLE=1` 时 WiFi 就绪后启动 WS（`dog_ws_mcp` 日志）
- [x] `tools/list` / `tools/call` → `self.get_device_status` 经 WS 往返
- [x] `self.camera.take_photo` 可达 MCP（摄像头未就绪时返回 error，仍证明链路）
- [ ] deep-trace 前端 WS 连接模式（Device 模块页 PoC 面板）

## 非目标（PoC）

- WS 鉴权 / TLS
- 替换 MQTT 远程控制
- 合并进 `deep_dog_http_server`（http-server 将逐步弃用）

## 与 http-server 关系

| 模块 | 状态 |
|------|------|
| `http-server/` | 设备内 Web 页 + MJPEG；**逐步弃用**，UI 迁 deep-trace |
| `ws-mcp/` | **长期保留** 的局域网 MCP 调试入口 |

二者勿同端口；同开时使用不同 `*_PORT` 宏。
