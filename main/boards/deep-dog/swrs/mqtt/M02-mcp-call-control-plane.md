# M02 · MQTT `mcp_call` 统一控制面（扩展规范）

| 项 | 内容 |
|----|------|
| 路线图 ID | **M02** |
| 依赖 | [M01](./M01-board-mqtt-protocol.md) · [WS-01](../ws-mcp/01-local-mcp-bridge.md) · 各模块 `FaceControl` / `McpServer` |
| 状态 | **规范就绪**（motor 已落地；face/stream 待按需扩展） |
| 真源 | 本文件 + [deep-dog-mqtt.yml](./protocol/deep-dog-mqtt.yml) |

## 1. 目标

- **控制指令**经 `McpServer::InvokeToolSync` 执行，MQTT / WS MCP / 语音共用同一套 `self.*` 工具。
- MQTT 模块只做 **Topic 适配 + status 发布**，不复制业务逻辑。
- **状态 / retain / 心跳** 仍走 MQTT 专用 Topic（不 MCP 化）。

## 2. 已落地模式

### 2.1 域 API 薄封装（face）

[`face_mqtt.cc`](../../mqtt/modules/face_mqtt.cc) → [`FaceControl`](../../face_ai/face_control.cc) → `face_ai_*`。

MQTT `face/cmd` 字段映射到 FaceControl；[`face_mcp.cc`](../../face_ai/face_mcp.cc) 注册聚合 MCP 工具。

### 2.2 `mcp_call` 透传（motor / can）

[`motor_mqtt.cc`](../../mqtt/modules/motor_mqtt.cc) 支持：

```json
{
  "mcp_call": {
    "name": "self.motor.scan_bus",
    "arguments": {}
  }
}
```

白名单：`self.motor.*`、`self.can.*`。结果经 `motor/status`（或模块 status）回传。

## 3. 扩展 face / stream（规划，未改 Topic 语义）

| 模块 | 建议 cmd 扩展 | MCP 工具 | 说明 |
|------|---------------|----------|------|
| face | 保留现有 `face/cmd` 字段 | `self.face.*`（已注册） | 新能力先加 MCP + FaceControl，MQTT 增字段 |
| stream | 可选 `mcp_call` | `self.camera.*` 等 | `take_photo` 等重栈操作继续独立 task |
| led/servo/dog | 可选 `mcp_call` 或域 API | 已有 `self.led_strip.*` / `self.dog.*` | 与 motor 同模式 |

**禁止**：用 MCP 替代 `face/status`、`face/registry` retain、`stream/status`、`device/status`。

## 4. 与 WS MCP 关系

| 通道 | 用途 |
|------|------|
| WS `ws://{ip}:{port}/ws` | 局域网调试；`tools/call` 直调 MCP |
| MQTT `mcp_call` | 远程 deep-trace / 脚本；适合已连 EMQX 的场景 |
| MQTT 原生 cmd | 前端 Steps 已写明的字段（face/stream/…） |

二者 **不是替代关系**；实现上必须落到同一 `InvokeToolSync`。

## 5. 验收

- [x] motor `mcp_call` 白名单 + 错误回传
- [x] face MQTT / MCP / FaceControl 三端 `set_mode` / `manage` / `list` 一致（V-S07）
- [ ] stream `mcp_call` PoC（可选，低优先级）
- [ ] deep-trace 前端文档引用本规范（REQ 侧）

## 6. 非目标

- 删除现有 MQTT Topic
- 设备 WS 公网暴露替代 EMQX
- 把 `device/status` 心跳改为 MCP 轮询
