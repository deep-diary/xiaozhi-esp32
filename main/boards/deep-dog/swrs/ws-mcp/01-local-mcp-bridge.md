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
| 远程 / 云控 | MQTT（`device/info` 含 `ip` + `ws_mcp_port` 供同网段发现后再连 WS） |
| 语音 / 云端 MCP | 与 WS 共用同一套 `AddTool`；查 IP 用 `self.board.get_ip`（勿指望本地 WS 引导自己） |

## 架构

- **独立 httpd**（与 `http-server/` 解耦，便于后续弃用设备内 Web UI）
- 开关：`DEEP_DOG_WS_MCP_ENABLE`（单电机剖面默认 **1**）
- 默认：`8080` + `/ws`（`DEEP_DOG_HTTP_SERVER_ENABLE=0` 时不冲突）
- MCP 响应：`RegisterMcpBroadcastCallback` → `BroadcastMessage` 到所有 WS 客户端
- httpd 任务栈 **4096 → 3072**：重剖面（face / vision / track / microros 全开）下 internal SRAM 最大连续块可能 <4096，`httpd_start` 会报 `ESP_ERR_HTTPD_TASK` 启动失败；减栈后若仍不足，需关闭非必要重型功能释放 internal（见 [S09](../vision/server/S09-internal-sram-optimization.md)）
- **客户端生命周期**：`clients_`（`std::map<int, httpd_req_t*>`）不依赖 WS `CLOSE` 帧清理。异常断开（浏览器关 tab / 刷新 / HMR 直丢 socket、RST / 半开、握手中断）时 httpd 内部删 session 且**不回调** ws handler，`CLOSE` 分支覆盖不到，会产生「`clients_` 还记着、底层 socket 已失效」的僵尸 fd。故注册 `HTTP_SERVER_EVENT_DISCONNECTED` 事件（覆盖全部 session 终止路径，事件数据为 `int fd`，派发发生在 httpd `close(fd)` 之后）在源头 `clients_.erase(fd)`；`clients_` 由 `std::mutex` 保护（`BroadcastMessage` 在 MCP 回调线程、`Add/RemoveClient` 在 httpd 任务、`OnDisconnected` 在 esp_event 任务，三方并发）。`WsBroadcastSendJob` 的 `send failed` 仅 `ESP_ERR_NO_MEM` 等真错误用 `ESP_LOGW`；`ESP_ERR_INVALID_ARG`（fd 已不在 session 表，属正常竞态）降为 `ESP_LOGD`，避免刷屏。

## 入站协议

1. **直发 JSON-RPC**（与 [`docs/mcp-usage_zh.md`](../../../../docs/mcp-usage_zh.md) 一致）
2. **信封**：`{"type":"mcp","payload":{...}}`

支持 `tools/list`、`tools/call`、`initialize` 等标准 MCP 方法。

## device/info 扩展（可选字段）

| 字段 | 说明 |
|------|------|
| `ip` | STA IPv4（DHCP）；Web 拼 WS URL 的主机 |
| `ws_mcp_port` | WS 监听端口（未启用则不出现） |
| `ws_mcp_path` | 默认 `/ws` |
| `capabilities.ws_mcp` | `true` |

## MCP（语音 / 云端）

| 工具 | 说明 |
|------|------|
| `self.board.get_ip` | 返回 `ip` / `connected` / `ws_url`；用户问「IP 是多少 / WebSocket 地址」时调用并口述 |
| `self.get_device_status` | `network.ip`（与 ssid/signal 同层）；问「网络状态」时一并给出 |

## 验收

- [x] `DEEP_DOG_WS_MCP_ENABLE=1` 时 WiFi 就绪后启动 WS（`dog_ws_mcp` 日志）
- [x] `tools/list` / `tools/call` → `self.get_device_status` 经 WS 往返
- [x] `self.camera.take_photo` 可达 MCP（摄像头未就绪时返回 error，仍证明链路）
- [x] `self.board.get_ip` 返回当前 STA IP 与 `ws_url`（语音口述 / 手动填 Web）
- [x] internal SRAM 不足时 httpd 栈减至 3072 仍可启动（`dog_ws_mcp` 无 `ESP_ERR_HTTPD_TASK`）
- [ ] 浏览器进入 MCP 页后立即退出 / 刷新，异常断开不再出现 `broadcast send failed fd=xx err=258` 刷屏，`client disconnected` 与 `client connected` 计数对称
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
