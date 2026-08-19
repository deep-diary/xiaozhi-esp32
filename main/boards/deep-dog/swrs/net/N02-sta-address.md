# N02 · Wi‑Fi STA 寻址（DHCP 默认）

| 项 | 内容 |
|----|------|
| ID | N02 |
| 依赖 | WiFi STA |
| 代码 | [`net/net_config.h`](../../net/net_config.h) · [`esp_sparkbot_board.cc`](../../esp_sparkbot_board.cc) |
| 关联 | [N01 SNTP](./N01-sntp-clock-sync.md) · [WS-01](../ws-mcp/01-local-mcp-bridge.md) |

## 目标

换局域网（家用 `192.168.31.0/24` vs 其它网段）时，**不必改固件里的网段/主机号**。设备 IP、网关、DNS 由 AP DHCP 下发。

## 行为（与 otto WS 同一套路）

| 项 | 做法 |
|----|------|
| STA IPv4 | **默认 DHCP**（`DEEP_DOG_WIFI_USE_STATIC_IP=0`） |
| 局域网 WS MCP | httpd 听 **`0.0.0.0:8080`**，**不绑定**某一网段。客户端连 `ws://{当前STA-IP}:8080/ws` |
| 地址从哪来 | 串口 `Got IP` / `WS MCP bridge ws://…`；**Web**：MQTT `device/info.ip` + `ws_mcp_port`/`ws_mcp_path` 拼 URL；**语音**：云端 MCP `self.board.get_ip` / `self.get_device_status.network.ip`（不必先连本地 WS） |

## Web / 语音如何得知 WS IP（发现路径）

| 客户端 | 推荐 | 说明 |
|--------|------|------|
| deep-trace Hub | **先 MQTT，再 WS** | 订 retain `device/info` → 读 `ip`、`ws_mcp_port`、`ws_mcp_path` → `ws://{ip}:{port}{path}`（已实现，见 REQ-IOT-236） |
| 手动调试页 | MQTT 自动填，或听语音/看串口后手输 | 鸡生蛋：本地 WS **不能**用来发现自己的 IP |
| otto 无自有 MQTT | 串口 / 小程序看 IP，**手输** `ws://…:8080/ws` | `self.otto.get_ip` 走**云端语音 MCP**，方便口述，不是 Web 发现真源 |

deep-dog 对齐：`self.board.get_ip`（含 `ws_url`）；`self.get_device_status` 的 `network.ip` 供「设备网络状态」类追问。

## 静态 IP（可选，仅固定实验室）

`net_config.h` 置 `DEEP_DOG_WIFI_USE_STATIC_IP=1` 并填 **当前** 网段的 IP/网关。换环境必须改宏或改回 DHCP。

错误配置（例如人在非 `31` 网段仍写 `192.168.31.211`）会导致：

1. STA 地址不在当前 LAN，对端无法访问本机 WS
2. DNS 被指到错误网关 → `getaddrinfo` 失败 → **官网 OTA**（`api.tenclass.net`）失败
3. 板级 MQTT / WS MCP 若延后到激活完成，会被 OTA 重试拖住（表象像「WS 挂了连不上官网」；根因是 **错误静态 IP/DNS**，不是 WS 协议）

## 配置

| 宏 | 默认 | 说明 |
|----|------|------|
| `DEEP_DOG_WIFI_USE_STATIC_IP` | **0** | 1=停 DHCP、用下方字面量 |
| `DEEP_DOG_WIFI_STATIC_IP_*` 等 | `192.168.31.211` | 仅 `USE_STATIC_IP=1` 时生效 |

## 验收

- [x] 默认剖面：`DEEP_DOG_WIFI_USE_STATIC_IP=0`（DHCP）；连任意带 DHCP 的 AP，串口出现路由器分配的 IP（不必是 `31` 网段）
- [x] 公网 DNS 可解析（OTA 或 `broker.emqx.io`）；不再因写死网关 `192.168.31.1` 出现 `getaddrinfo 202`
- [x] 激活完成后 WS 日志含 `ws://{该IP}:8080/ws`（例：`ws://192.168.3.84:8080/ws`）
- [x] MCP `self.board.get_ip` / `get_device_status.network.ip` 可供语音口述
- [ ] 静态 IP=1 仍可在固定实验室使用（文档标明须匹配当前网段）
